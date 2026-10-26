"""Pico target selection, linked resource accounting and packaged UF2 safety."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_pico_ram import ram_report
from check_pico_ha_stack import stack_report
from audit_pico_stack import audit, parse_graph, cmake_enabled
from pico_stack_paths import parse_machine, path_audit, account, EXTRA_ROOTS, IRQ_ROOTS
from pico_stack_fingerprints import dependencies, verify_files
from pico_stack_contracts import enabled_target
from unittest.mock import patch
from package_web_firmware import package, validate_pico_uf2


def uf2(count=2, family=0xE48BFF59, workaround=True):
    data = bytearray()
    if workaround:
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, 0x0A324655, 0x9E5D5157, 0xA000,
                         0x10FFFF00, 256, 0, 2, 0xE48BFF57)
        block[32:288] = b"\xef" * 256
        struct.pack_into("<I", block, 288, 0x9957E304)
        struct.pack_into("<I", block, 508, 0x0AB16F30)
        data.extend(block)
    for n in range(count):
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, 0x0A324655, 0x9E5D5157, 0x2000,
                         0x10000000 + n * 256, 256, n, count, family)
        struct.pack_into("<I", block, 508, 0x0AB16F30)
        data.extend(block)
    return data


class PicoUf2Tests(unittest.TestCase):
    def test_chip_families_are_not_interchangeable(self):
        for board in ("pico", "pico_w"):
            validate_pico_uf2(uf2(family=0xE48BFF56, workaround=False), 0x1017E000, board)
            with self.assertRaisesRegex(ValueError, "family"):
                validate_pico_uf2(uf2(workaround=False), 0x1017E000, board)
        validate_pico_uf2(uf2(), 0x1037E000, "pico2_w")
        with self.assertRaisesRegex(ValueError, "family"):
            validate_pico_uf2(uf2(family=0xE48BFF56), 0x1037E000, "pico2_w")

    def test_workaround_is_strictly_validated(self):
        for offset in (8, 12, 16, 20, 24, 32, 288, 292, 508):
            candidate = uf2()
            candidate[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                validate_pico_uf2(candidate, 0x1037E000, "pico2_w")
        with self.assertRaisesRegex(ValueError, "no application"):
            validate_pico_uf2(uf2()[:512], 0x1037E000, "pico2_w")

    def test_reserved_storage_and_malformed_blocks_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "reserved"):
            validate_pico_uf2(uf2(count=17), 0x10001000, "pico2_w")
        for offset in (0, 8, 12, 16, 20, 24, 28, 508):
            candidate = uf2()
            candidate[512 + offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                validate_pico_uf2(candidate, 0x1037E000, "pico2_w")
        for candidate in (b"", uf2()[:-1], uf2() + uf2()):
            with self.assertRaises(ValueError):
                validate_pico_uf2(candidate, 0x1037E000, "pico2_w")

    def test_package_records_chip_and_checks_layout_board(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "lightvm.uf2"
            image.write_bytes(uf2())
            layout = {"board": "pico2_w", "storage_start": 0x10380000,
                      "storage_bytes": 524288, "flash_bytes": 4194304, "firmware_end": 0x1037E000}
            layout_file = root / "storage-layout.json"
            layout_file.write_text(json.dumps(layout))
            package(root / "out", {}, {"pico2_w": image})
            manifest = json.loads((root / "out/pico.json").read_text())
            self.assertEqual(manifest["builds"][0]["chip"], "RP2350")
            self.assertEqual((root / "out/pico2_w/lightvm.uf2").read_bytes(), image.read_bytes())
            for key, value in (("board", "pico_w"), ("flash_bytes", 2097152),
                               ("firmware_end", 0x10381000), ("storage_bytes", True)):
                layout_file.write_text(json.dumps({**layout, key: value}))
                with self.subTest(key=key), self.assertRaisesRegex(ValueError, "layout"):
                    package(root / "out", {}, {"pico2_w": image})


class PicoStackInventoryTests(unittest.TestCase):
    def test_feature_flags_follow_cmake_false_constants(self):
        for value in ('', '0', 'OFF', 'no', 'FALSE', 'N', 'IGNORE', 'NOTFOUND', 'library-NOTFOUND'):
            self.assertFalse(cmake_enabled(value), value)
        for value in ('ON', 'true', '1', '2', 'YES', 'Y'):
            self.assertTrue(cmake_enabled(value), value)

    def test_unknown_and_recursive_edges_are_not_certified_as_zero(self):
        text='''graph: { title: "test"
node: { title: "root" label: "root\\ntest.c:1:1\\n64 bytes (static)" }
node: { title: "child" label: "child\\ntest.c:2:1\\n128 bytes (static)" }
edge: { sourcename: "root" targetname: "child" }
edge: { sourcename: "child" targetname: "child" }
edge: { sourcename: "child" targetname: "__indirect_call" }
}'''
        report=audit(*parse_graph([text]),roots=('root','missing'))
        self.assertFalse(report['certified'])
        root=report['roots']['root']
        self.assertFalse(root['complete'])
        self.assertEqual(root['known_frame_sum_bytes'],192)
        self.assertTrue(any('indirect' in item for item in root['unresolved']))
        self.assertTrue(any('recursive' in item for item in root['unresolved']))
        self.assertIsNone(report['roots']['missing']['known_frame_sum_bytes'])


class PicoClosedStackTests(unittest.TestCase):
    def test_interrupt_roots_do_not_depend_on_sdk_checkout_location(self):
        handlers=(
            'dcd_int_handler',
            'src/rp2_common/pico_stdio_usb/stdio_usb.c:low_priority_worker_irq',
            'src/common/pico_time/time.c:alarm_pool_irq_handler',
            'src/rp2_common/hardware_gpio/gpio.c:gpio_default_irq_handler',
            'src/rp2_common/pico_multicore/multicore.c:multicore_lockout_handler',
        )
        for sdk in ('build/deps/pico-sdk', 'build/deps/pico-sdk-2.3.1', '/opt/pico/sdk'):
            with self.subTest(sdk=sdk):
                frames={sdk+'/'+name if '/' in name else name:32 for name in handlers}
                report=path_audit(frames,{},dict.fromkeys(frames,'static'),IRQ_ROOTS)
                self.assertTrue(all(result['complete'] for result in report.values()))
                self.assertTrue(all(result['foreground_bytes']==32 for result in report.values()))
                # A similarly named function from another source cannot fill a missing root.
                missing=IRQ_ROOTS[1]
                frames[sdk+'/unreviewed.c:'+missing.rsplit(':',1)[1]]=frames.pop(sdk+'/'+missing)
                report=path_audit(frames,{},dict.fromkeys(frames,'static'),IRQ_ROOTS)
                self.assertFalse(report[missing]['complete'])
                self.assertIn('missing root: '+missing,report[missing]['unresolved'])

    def test_disabled_features_only_remove_their_known_registrations(self):
        minimal=dict.fromkeys(('LIGHTVM_WIFI','LIGHTVM_HTTP','LIGHTVM_MQTT'),False)
        for name in ('http_tick','ha_tick','mqtt_tick','src/home_assistant.c:response','shared/device-core/src/http.c:capture','platforms/pico/network_port.c:scan','cyw43_gpio_irq_handler'):
            self.assertFalse(enabled_target(name,minimal),name)
        self.assertTrue(enabled_target('new_unknown_callback',minimal))
        http={**minimal,'LIGHTVM_WIFI':True,'LIGHTVM_HTTP':True}
        self.assertTrue(enabled_target('shared/device-core/src/http.c:capture',http))
        self.assertFalse(enabled_target('src/home_assistant.c:response',http))

    def test_lwip_callbacks_follow_each_configured_protocol(self):
        for http in (False,True):
            for mqtt in (False,True):
                features={'LIGHTVM_WIFI':True,'LIGHTVM_HTTP':http,'LIGHTVM_MQTT':mqtt}
                with self.subTest(features=features):
                    for name in ('tcp_tmr','tcpip_tcp_timer'):
                        self.assertEqual(enabled_target(name,features),http or mqtt)
                    for name in ('dns_tmr','dns_recv'):
                        self.assertEqual(enabled_target(name,features),mqtt)
                    self.assertEqual(enabled_target('igmp_tmr',features),http)
                    self.assertTrue(enabled_target('new_unknown_callback',features))
        minimal=dict.fromkeys(('LIGHTVM_WIFI','LIGHTVM_HTTP','LIGHTVM_MQTT'),False)
        audio={**minimal,'LIGHTVM_WIFI':True,'LIGHTVM_AUDIO_MULTICAST':True}
        self.assertTrue(enabled_target('audio_received',audio))
        self.assertTrue(enabled_target('igmp_tmr',audio))
        self.assertFalse(enabled_target('audio_received',minimal))

    def test_machine_boundaries_returns_veneers_and_dynamic_reservations(self):
        text = """00001000 <caller>:
1000:\tb510      \tpush\t{r4, lr}
1002:\tb088      \tsub\tsp, #0x20
1004:\tf000 f804 \tbl\t1010 <callee>
1008:\tbd10      \tpop\t{r4, pc}
00001010 <callee>:
1010:\tb500      \tpush\t{lr}
1012:\tbc08      \tpop\t{r3}
1014:\tb002      \tadd\tsp, #8
1016:\t4718      \tbx\tr3
00001020 <dynamic>:
1020:\tebad 0d00 \tsub.w\tsp, sp, r0
1024:\t4798      \tblx\tr3
00001030 <__callee_veneer>:
1030:\tb401      \tpush\t{r0}
1032:\t4802      \tldr\tr0, [pc, #8]
1034:\t4684      \tmov\tip, r0
1036:\tbc01      \tpop\t{r0}
1038:\t4760      \tbx\tip
103a:\tbf00      \tnop
103c:\t00001011  \t.word\t0x00001011
"""
        frames,edges,indirect,unsafe,_=parse_machine(text,[(0x1000,10,'caller'),(0x1010,8,'callee'),(0x1020,6,'dynamic'),(0x1030,16,'__callee_veneer')])
        self.assertEqual(frames[0x1000],40)
        self.assertEqual(edges[0x1000],{0x1010})
        self.assertEqual(edges[0x1030],{0x1010})
        self.assertEqual(indirect,{0x1020})
        self.assertEqual(unsafe,{0x1020})

    def test_unknown_dynamic_and_unguarded_recursion_fail_closed(self):
        frames={'root':64,'child':128,'runtime_checkpoint':16}
        edges={'root':{'child'},'child':{'child','__indirect_call'}}
        result=path_audit(frames,edges,{k:'static' for k in frames},('root',))['root']
        self.assertFalse(result['complete'])
        self.assertTrue(any('recursive' in e for e in result['unresolved']))
        self.assertTrue(any('indirect' in e for e in result['unresolved']))
        result=path_audit({'root':64},{},{'root':'dynamic'},('root',))['root']
        self.assertFalse(result['complete'])
        # Real deferred preparation must not recursively run the service hook.
        frames={'preparation_tick':64,'runtime_checkpoint':16}
        result=path_audit(frames,{'preparation_tick':{'runtime_checkpoint'},'runtime_checkpoint':{'preparation_tick'}},{k:'static' for k in frames},('preparation_tick',))['preparation_tick']
        self.assertTrue(result['complete'])
        self.assertEqual(result['foreground_bytes'],80)

    def test_context_dependent_printf_paths_do_not_share_short_memo_entries(self):
        frames={'root':1,'printf:_ftoa':2,'printf:_etoa':3}
        edges={'root':{'printf:_ftoa','printf:_etoa'},'printf:_ftoa':{'printf:_etoa'},'printf:_etoa':{'printf:_ftoa'}}
        report=path_audit(frames,edges,dict.fromkeys(frames,'static'),('root',))['root']
        self.assertTrue(report['complete'])
        self.assertEqual(report['foreground_bytes'],8)

    def test_interrupts_and_full_path_overflow_are_in_the_gate(self):
        roots=('main','platforms/pico/render_executor.c:worker_main')
        frames={name:32 for name in roots+EXTRA_ROOTS+IRQ_ROOTS}
        frames['main']=15000
        with tempfile.TemporaryDirectory() as directory:
            elf=Path(directory)/'app.elf';elf.write_bytes(b'fixture')
            with patch('pico_stack_paths.close_graph',return_value=(frames,{},dict.fromkeys(frames,'static'),[],'fixture')), patch('pico_stack_paths.read_symbols',return_value={'__StackTop':16384,'__StackBottom':0,'__StackOneTop':8192,'__StackOneBottom':0}):
                report=account({}, {}, {},None,elf,roots)
        self.assertTrue(report['software_accounting_complete'])
        self.assertFalse(report['software_gate_passed'])
        self.assertGreater(report['main']['with_interrupts_bytes'],15000)
        self.assertFalse(report['certified'])

    def test_worker_gate_uses_the_linked_reservation(self):
        roots=('main','platforms/pico/render_executor.c:worker_main')
        frames={name:32 for name in roots+EXTRA_ROOTS+IRQ_ROOTS}
        frames[roots[1]]=4096
        with tempfile.TemporaryDirectory() as directory:
            elf=Path(directory)/'app.elf';elf.write_bytes(b'fixture')
            with patch('pico_stack_paths.close_graph',return_value=(frames,{},dict.fromkeys(frames,'static'),[],'fixture')), patch('pico_stack_paths.read_symbols') as symbols:
                symbols.return_value={'__StackTop':16384,'__StackBottom':0,'__StackOneTop':8192,'__StackOneBottom':0}
                self.assertTrue(account({}, {}, {},None,elf,roots)['software_gate_passed'])
                symbols.return_value['__StackOneTop']=4096
                report=account({}, {}, {},None,elf,roots)
                self.assertFalse(report['software_gate_passed'])
                self.assertEqual(report['worker']['reservation_bytes'],4096)

    def test_audited_source_edits_require_review(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'callback.c';source.write_text('first')
            manifest={'project':{'callback.c':hashlib.sha256(source.read_bytes()).hexdigest()},'sdk':{}}
            self.assertFalse(verify_files(manifest,root,root))
            source.write_text('new callback')
            self.assertEqual(len(verify_files(manifest,root,root)),1)

    def test_firmware_fingerprints_exclude_native_sdk_build_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);sdk=root/'sdk';sdk.mkdir();build=root/'build'
            for name in ('firmware.h','core.h','host-tool.h'):
                (sdk/name).write_text('fixture')
            for target,header in (
                ('CMakeFiles/lightvm.dir/port.c.o.d','firmware.h'),
                ('CMakeFiles/lightvm_core.dir/core.c.o.d','core.h'),
                ('_deps/picotool-build/CMakeFiles/picotool.dir/main.cpp.o.d','host-tool.h'),
                ('pioasm/CMakeFiles/pioasm.dir/main.cpp.o.d','host-tool.h'),
            ):
                file=build/target;file.parent.mkdir(parents=True,exist_ok=True)
                file.write_text('object.o: '+str(sdk/header)+'\n')
            self.assertEqual(dependencies(build,sdk),{'firmware.h','core.h'})


class PicoHaStackTests(unittest.TestCase):
    def usage(self, message=584, kind="static"):
        return (f"/src/home_assistant.c:30:13:message\t{message}\t{kind}\n"
                "/src/home_assistant.c:90:13:light_command.constprop.0\t5400\tstatic\n"
                "/src/home_assistant.c:166:6:ha_tick\t2592\tstatic\n")

    def test_parser_frame_cannot_remain_live_during_dispatch(self):
        self.assertEqual(stack_report(self.usage())["message"], 584)
        with self.assertRaisesRegex(ValueError, "message stack frame"):
            stack_report(self.usage(message=6328))

    def test_missing_or_unbounded_measurement_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "non-static"):
            stack_report(self.usage(kind="dynamic"))
        with self.assertRaisesRegex(ValueError, "missing"):
            stack_report("")


class PicoRamTests(unittest.TestCase):
    def symbols(self, kib=520):
        top = 0x20000000 + kib * 1024
        return {"__lightvm_sram_start": 0x20000000, "__lightvm_sram_end": top,
                "__end__": 0x20020000, "__scratch_x_start__": top - 16384,
                "__scratch_x_end__": top - 16384, "__scratch_y_start__": top - 16384,
                "__scratch_y_end__": top - 16384, "__StackTop": top,
                "__StackBottom": top - 16384, "__HeapLimit": top - 16384, "__StackOneTop": top - 16384,
                "__StackOneBottom": top - 16384, "__lightvm_vm_heap": 65536,
                "__lightvm_native_headroom": 16384,
                "__lightvm_pixel_capacity": 1024, "__lightvm_output_slots": 7}

    def test_target_geometry_and_compiled_limits(self):
        small, large = ram_report(self.symbols(264)), ram_report(self.symbols())
        self.assertEqual(large["remaining_bytes_after_reservations"] - small["remaining_bytes_after_reservations"], 256 * 1024)
        symbols = self.symbols()
        symbols["__lightvm_vm_heap"] *= 2
        self.assertEqual(ram_report(symbols)["remaining_bytes_after_reservations"], large["remaining_bytes_after_reservations"] - 65536)
        symbols["__lightvm_pixel_capacity"] = 175
        self.assertEqual(ram_report(symbols)["pixel_driver_max"], 7 * (175 * 4 + 256))

    def test_ram_code_scratch_and_stacks_are_included(self):
        symbols = self.symbols()
        expected = ram_report(symbols)["remaining_bytes_after_reservations"]
        symbols["__end__"] += 1024
        symbols["__scratch_x_end__"] += 256
        symbols["__StackOneBottom"] -= 512
        symbols["__HeapLimit"] -= 512
        self.assertEqual(ram_report(symbols)["remaining_bytes_after_reservations"], expected - 1792)
        symbols["__lightvm_vm_heap"] = 520 * 1024
        self.assertLess(ram_report(symbols)["remaining_bytes_after_reservations"], 0)

    def test_unused_sram_cannot_hide_a_small_or_overlapping_heap(self):
        symbols = self.symbols()
        symbols["__HeapLimit"] = symbols["__end__"] + 65536
        self.assertLess(ram_report(symbols)["remaining_bytes_after_reservations"], 0)
        symbols["__HeapLimit"] = symbols["__StackBottom"] + 256
        with self.assertRaisesRegex(ValueError, "heap/stack"):
            ram_report(symbols)
        symbols = self.symbols()
        symbols["__StackOneBottom"] -= 8192
        with self.assertRaisesRegex(ValueError, "core-1 heap/stack"):
            ram_report(symbols)
        symbols["__HeapLimit"] = symbols["__StackOneBottom"]
        self.assertEqual(ram_report(symbols)["native_stack"], 24576)

    def test_enabled_dcp_must_survive_linker_garbage_collection(self):
        symbols = self.symbols()
        symbols["__lightvm_dcp_enabled"] = 1
        with self.assertRaisesRegex(ValueError, "wrappers were not linked"):
            ram_report(symbols)
        for operation in ("dadd", "dsub", "dmul", "ddiv"):
            symbols["__wrap___aeabi_" + operation] = 0x10001000
        self.assertGreater(ram_report(symbols)["remaining_bytes_after_reservations"], 0)


@unittest.skipUnless(shutil.which("cmake") or os.environ.get("CMAKE"), "CMake not installed")
class PicoBoardSelectionTests(unittest.TestCase):
    def select(self, *settings):
        return subprocess.run([os.environ.get("CMAKE", "cmake"), *settings, "-P",
                               str(ROOT / "platforms/pico/select_board.cmake")],
                              capture_output=True, text=True,
                              env={k: v for k, v in os.environ.items() if k not in ("PICO_BOARD", "PICO_PLATFORM")})

    def test_supported_boards_and_conflicts(self):
        self.assertEqual(self.select().returncode, 0)
        for board in ("pico", "pico_w", "pico2_w"):
            with self.subTest(board=board):
                self.assertEqual(self.select(f"-DPICO_BOARD={board}").returncode, 0)
        for settings, message in (
            (("-DPICO_BOARD=unknown",), "Unsupported PICO_BOARD"),
            (("-DPICO_BOARD=pico2_w", "-DPICO_PLATFORM=rp2350-riscv"), "requires PICO_PLATFORM=rp2350-arm-s"),
            (("-DPICO_BOARD=pico2_w", "-DLIGHTVM_PICO_BOARD=pico_w"), "fresh build directory"),
        ):
            result = self.select(*settings)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
