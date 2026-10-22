"""Close GCC's stack graph using configured callbacks and linked machine code."""
import hashlib
import re
import subprocess
from pathlib import Path
from pico_stack_contracts import CALLBACKS, SDK_CALLBACKS, ASSEMBLY_PATHS, enabled_target
from check_pico_ram import read_symbols

CLONE = re.compile(r'\.(?:part|isra|constprop|cold)\.\d+')
def base(name):
    return CLONE.sub('', name)

def parse_machine(text, symbols):
    """Decode reservations and call edges using symbol *ranges*, not labels.

    GNU ld can omit a weak local function's label while leaving its body between
    two named functions. Treat direct calls into such a gap as separate bodies;
    otherwise its stack and indirect call would contaminate the preceding one.
    """
    import bisect
    instructions=[]
    branch=re.compile(r'b(?:l|lx|eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(?:\.w|\.n)?$')
    for line in text.splitlines():
        match=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2,8}\s+)+\t([.a-z][a-z0-9.]*)\s*(.*)',line)
        if match:instructions.append((int(match[1],16),match[2],match[3].split('@')[0].strip()))
    bounds={addr:(size,label.removeprefix('.hidden ')) for addr,size,label in symbols}
    labels={int(m[1],16):m[2] for m in re.finditer(r'^([0-9a-f]+) <([^>]+)>:',text,re.M)}
    for addr,label in labels.items():
        if addr in bounds:bounds[addr]=(bounds[addr][0],label)
        elif label in ('fdiv_save_state',):bounds[addr]=(0,label)
    starts=sorted(bounds);zero={a for a,(size,label) in bounds.items() if not size}
    for index,addr in enumerate(starts):
        size,label=bounds[addr]
        if not size:bounds[addr]=((starts[index+1]-addr) if index+1<len(starts) else 2,label)
    def owner(addr):
        index=bisect.bisect_right(starts,addr)-1
        if index>=0 and addr<starts[index]+bounds[starts[index]][0]:return starts[index]
        return None
    # A BL can target a non-F assembly helper inside a zero-sized entry.
    for address,op,args in instructions:
        target=re.search(r'([0-9a-f]+) <[^>]+>',args) if op in ('bl','bl.w','blx') else None
        if target:
            addr=int(target[1],16);parent=owner(addr)
            if parent in zero and addr!=parent:
                end=parent+bounds[parent][0];bounds[parent]=(addr-parent,bounds[parent][1])
                bounds[addr]=(end-addr,labels.get(addr,'anonymous'));zero.add(addr);starts=sorted(bounds)
    # Recover anonymous local bodies reached by a direct call or tail jump.
    for address,op,args in instructions:
        target=re.search(r'([0-9a-f]+) <[^>]+>',args) if branch.fullmatch(op) else None
        if target and owner(int(target[1],16)) is None:
            addr=int(target[1],16);end=next((n for n in starts if n>addr),addr+2)
            bounds[addr]=(end-addr,'anonymous');starts=sorted(bounds)
    groups={addr:[] for addr in starts}
    for address,op,args in instructions:
        start=owner(address)
        if start is not None:groups[start].append((op,args))
    frames={a:0 for a in starts};edges={};indirect=set();unsafe=set()
    for name,code in groups.items():
        returned=None
        for op,args in code:
            if op.startswith(('push','vpush')) or op in ('stmdb','stmdb.w','vstmdb','vstmdb.w') and args.startswith('sp!'):
                regs=re.search(r'\{([^}]+)\}',args)
                if regs:
                    for reg in regs[1].split(','):
                        reg=reg.strip();span=re.fullmatch(r'([rsd])(\d+)-[rsd](\d+)',reg)
                        frames[name]+=(int(span[3])-int(span[2])+1)*(8 if span[1]=='d' else 4) if span else 8 if reg.startswith('d') else 4
            if op.startswith('sub') and re.match(r'sp(?:,\s*sp)?,',args):
                amount=re.search(r'#(0x[0-9a-f]+|\d+)',args)
                if amount:frames[name]+=int(amount[1],0)
                else:unsafe.add(name)
            if op.startswith('str') and re.search(r'\[sp,\s*#-(\d+)\]!',args):
                frames[name]+=int(re.search(r'#-(\d+)',args)[1])
            if branch.fullmatch(op) or op in ('cbz','cbnz'):
                target=re.search(r'([0-9a-f]+) <[^>]+>',args)
                if target:
                    dest=owner(int(target[1],16))
                    if dest is None:unsafe.add(name)
                    elif dest!=name or op in ('bl','bl.w','blx') and int(target[1],16)==name:edges.setdefault(name,set()).add(dest)
                elif op in ('bl','bl.w','blx'):indirect.add(name)
            if op in ('bx','bx.w') and args not in ('lr','r14',returned):indirect.add(name)
            if op.startswith('mov') and args.startswith('sp,'):unsafe.add(name)
            # Thumb-1 variadic epilogues pop their saved return PC into r3,
            # discard stacked arguments, then bx r3. Only this narrow pattern
            # is accepted as a return; other register branches stay unresolved.
            pop=re.fullmatch(r'\{(r[0-9]+)\}',args) if op=='pop' else None
            if pop and any('lr' in a and o=='push' for o,a in code):returned=pop[1]
            elif not (op.startswith('add') and args.startswith('sp,')):returned=None
        # Assembly entry labels can fall through into the next helper.
        executable=[(o,a) for o,a in code if not o.startswith('.') and o not in ('nop','nop.w')]
        if name in zero and executable:
            op,args=executable[-1]
            returns=op.startswith('pop') and 'pc' in args or op=='bx' or op in ('b','b.n','b.w')
            following=name+bounds[name][0]
            if not returns and following in frames:edges.setdefault(name,set()).add(following)
        if bounds[name][1].endswith('_veneer') and name in indirect:
            literals=[int(a,0)&~1 for o,a in code if o=='.word']
            allowed={'push','pop','ldr','ldr.w','mov','bx','nop','nop.w','.word'}
            if len(literals)==1 and all(o in allowed for o,a in code) and owner(literals[0]) is not None:
                indirect.remove(name);edges.setdefault(name,set()).add(owner(literals[0]))
    return frames,edges,indirect,unsafe,bounds


def machine_graph(objdump,elf,originals):
    text=subprocess.check_output([str(objdump),'-d',str(elf)],text=True)
    table=subprocess.check_output([str(objdump),'-t',str(elf)],text=True)
    symbols=[]
    for line in table.splitlines():
        match=re.match(r'([0-9a-f]+)\s+.*?\bF\s+\S+\s+([0-9a-f]+)\s+(.+)',line)
        if match:symbols.append((int(match[1],16),int(match[2],16),match[3].strip()))
    frames,edges,indirect,unsafe,bounds=parse_machine(text,symbols)
    addresses=sorted(frames)
    addr2line=str(objdump).replace('objdump','addr2line')
    lines=subprocess.check_output([addr2line,'-f','-e',str(elf)],input=''.join(f'{a:x}\n' for a in addresses),text=True).splitlines()
    root=str(Path(__file__).resolve().parents[1])+'/'
    names={}
    for index,addr in enumerate(addresses):
        function,file=lines[index*2:index*2+2];file=file.split(':',1)[0].removeprefix(root)
        label=bounds[addr][1]
        if label=='anonymous':label=function
        specific=file+':'+label
        names[addr]=specific if specific in originals else label
        if names[addr] not in originals and list(names.values()).count(names[addr])>1:names[addr]+=f'@{addr:x}'
    def name(addr):return names[addr]
    return ({name(a):v for a,v in frames.items()},
            {name(a):{'@linked:'+name(b) for b in v} for a,v in edges.items()},
            {name(a) for a in indirect},{name(a) for a in unsafe},
            hashlib.sha256(text.encode()).hexdigest())


def close_graph(frames, edges, kinds, objdump, elf, features=None):
    machine, calls, indirect, unsafe, digest = machine_graph(objdump,elf,set(frames))
    frames, edges, kinds = dict(frames), {k:set(v) for k,v in edges.items()}, dict(kinds)
    originals = set(frames)
    nm = Path(str(objdump).replace('objdump','nm'))
    symbols = subprocess.check_output([str(nm), '-n', str(elf)], text=True)
    addresses, weak_null = {}, set()
    for line in symbols.splitlines():
        fields=line.split()
        if len(fields)==3:addresses.setdefault(fields[0],[]).append(fields[2])
        elif len(fields)==2 and fields[0] in ('w','v'):weak_null.add(fields[1])
    aliases={}
    for names in addresses.values():
        actual=next((n for n in names if n in machine),None)
        if actual:
            for n in names:aliases[n]=actual
    wrapped_symbols=set(re.findall(r'--wrap[=,]([A-Za-z0-9_]+)', (elf.parent/'CMakeFiles/lightvm.dir/link.txt').read_text()))
    by_short = {}
    for name in originals:
        by_short.setdefault(name.rsplit(':',1)[-1],set()).add(name)
    # These TinyUSB weak hooks are guarded by non-null tests. GNU ld folds
    # their unresolved weak addresses to zero and omits them from this ELF.
    nullable = {"tud_cdc_"+n+"_cb" for n in ('line_state','rx','rx_wanted','send_break','tx_complete')}
    weak_null.update(n for n in nullable if n not in machine and n not in by_short)
    def resolve(name):
        if name.startswith('@linked:'):return {name[8:]}
        if name in weak_null:return set()
        if name.startswith('__real_'):name=name[7:]
        elif name in wrapped_symbols:name='__wrap_'+name
        if name in aliases and name not in frames and name not in by_short:name=aliases[name]
        if name in frames:return {name}
        matches={n for n in originals if base(n)==name or base(n).endswith(':'+name)}
        if matches:return matches
        wrapped='__wrap_'+name
        if wrapped in frames:return {wrapped}
        if wrapped in by_short:return by_short[wrapped]
        if name in by_short:return by_short[name]
        if wrapped in machine:return {wrapped}
        if name in machine:return {name}
        return {name}
    for name,size in machine.items():
        if name in originals:
            frames[name]=max(frames[name],size)
            edges.setdefault(name,set()).update(calls.get(name,()))
            if name in unsafe and kinds[name]!='static':kinds[name]='dynamic-machine'
        else:
            frames[name]=size;kinds[name]='machine-sum' if name not in unsafe else 'dynamic-machine'
            edges[name]=set(calls.get(name,()))
        if name in indirect:edges[name].add('__indirect_call')
    contracts=[]
    for name in list(edges):
        if '__indirect_call' in edges[name]:
            key=base(name);targets=CALLBACKS.get(key)
            if key in ('runtime_run_initializers','runtime_run_per_core_initializers'):
                prefix='runtime_init_per_core_' if 'per_core' in key else 'runtime_init_'
                targets=tuple(n for n in originals if n.rsplit(':',1)[-1].startswith(prefix))
            if targets is None:targets=SDK_CALLBACKS.get(key.rsplit(':',1)[-1])
            if targets is not None:
                edges[name].remove('__indirect_call')
                # Some registered alternatives are compiled out on a board.
                resolved=set()
                for target in targets:
                    if enabled_target(target,features):resolved.update(resolve(target))
                edges[name].update(resolved)
                contracts.append({'caller':name,'targets':sorted(resolved),'declared':list(targets)})
        edges[name]={target for t in edges[name] for target in resolve(t)}
    for marker,group in (('generic_save_state','dcp'),('frr_core','vfp_trig')):
        if marker in machine:
            for name,(size,targets) in ASSEMBLY_PATHS[group].items():
                if name in frames:
                    frames[name]=size;edges[name]=set(targets);kinds[name]='audited-assembly'
                    contracts.append({'caller':name,'audited_assembly_bytes':size,'group':group,'targets':list(targets)})
    # Boot ROM is outside the ELF. These are explicitly budgeted boundaries,
    # recorded separately from compiler/machine frames, never silently zero.
    rom={'@rom_lookup':64,'@rom_flash':512,'@boot2':256,'@rom_float':256,'@rom_memory':128,'@rom_info':512,'@dcp_body':8}
    for n,size in rom.items():frames[n]=size;kinds[n]='external-budget'
    return frames,edges,kinds,contracts,digest


def path_audit(frames,edges,kinds,roots):
    report={}
    for root in roots:
        candidates=[n for n in frames if base(n)==root or base(n).endswith('/'+root)]
        memo={}
        def visit(name,active,service=False,job=0,worker=False,deferred=False,ethernet=False,acd_decline=False):
            exponential=any(base(x[0]).endswith(':_etoa') for x in active)
            key=(name,service,job,worker,deferred,ethernet,acd_decline,exponential)
            # Checkpoints never re-enter service from worker/deferred/service.
            if name=='runtime_checkpoint' and (service or worker or deferred):return frames.get(name,0),[name],set()
            if name=='app_dispatch' and service:
                total,path,gaps=visit('send_status',active,service,job,worker,deferred,ethernet,acd_decline)
                return frames[name]+total,[name+' [busy response]']+path,gaps
            if base(name)=='acd_netif_ip_addr_changed' and acd_decline:return frames[name],[name+' [address ANY]'],set()
            acd_decline=acd_decline or base(name)=='acd_netif_ip_addr_changed'
            if name=='__wrap_cyw43_send_ethernet' and ethernet:return frames[name],[name+' [re-entry rejected]'],set()
            ethernet=ethernet or name=='__wrap_cyw43_send_ethernet'
            if key in active:
                return 0,[],{'recursive edge: '+' -> '.join(x[0] for x in active[active.index(key):])+' -> '+name}
            if key in memo:return memo[key]
            if name not in frames:return 0,[name],{'missing frame: '+name}
            gaps=set()
            if kinds[name] not in ('static','machine-sum','audited-assembly','external-budget'):gaps.add('unbounded frame: '+name)
            service=service or base(name)=='src/app.c:service'
            worker=worker or name.endswith(':worker_main')
            deferred=deferred or name in ('app_init','preparation_tick','vm_tick_collect','vm_tick_flush')
            if name=='render_job_execute':job=min(job+1,2)
            maximum,path=0,[]
            for target in sorted(edges.get(name,())):
                # Native child jobs contain no guest invocation. A root job may
                # be a VM slice, a pixel range, a map or a stack probe.
                if name=='render_job_execute' and job>1 and target=='animation_execute':continue
                if acd_decline and base(name).endswith(':dhcp_conflict_callback') and base(target).endswith(':dhcp_bind'):continue
                # Exponential printf normalizes its argument before _ftoa;
                # that nested _ftoa cannot select exponential formatting again.
                if base(name).endswith(':_ftoa') and base(target).endswith(':_etoa') and exponential:continue
                total,branch,unknown=visit(target,active+(key,),service,job,worker,deferred,ethernet,acd_decline)
                gaps.update(('indirect caller: '+name) if x=='missing frame: __indirect_call' else x for x in unknown)
                if total>maximum:maximum,path=total,branch
            result=frames[name]+maximum,[name]+path,gaps
            memo[key]=result
            return result
        maximum,path,gaps=0,[],set()
        if not candidates:gaps.add('missing root: '+root)
        for name in candidates:
            total,branch,unknown=visit(name,());gaps.update(unknown)
            if total>maximum:maximum,path=total,branch
        report[root]={'complete':not gaps,'foreground_bytes':maximum,'path':path,'unresolved':sorted(gaps)}
    return report

IRQ_ROOTS = (
    'dcd_int_handler',
    'build/deps/pico-sdk-2.3.1/src/rp2_common/pico_stdio_usb/stdio_usb.c:low_priority_worker_irq',
    'build/deps/pico-sdk-2.3.1/src/common/pico_time/time.c:alarm_pool_irq_handler',
    'build/deps/pico-sdk-2.3.1/src/rp2_common/hardware_gpio/gpio.c:gpio_default_irq_handler',
    'build/deps/pico-sdk-2.3.1/src/rp2_common/pico_multicore/multicore.c:multicore_lockout_handler',
)
EXTRA_ROOTS = ('http_tick','mqtt_tick','app_execute_command','preparation_tick','store_write_step','core1_wrapper')

def account(frames,edges,kinds,objdump,elf,roots,features=None):
    frames,edges,kinds,contracts,digest=close_graph(frames,edges,kinds,objdump,elf,features)
    paths=path_audit(frames,edges,kinds,tuple(n for n in roots+EXTRA_ROOTS if enabled_target(n,features)))
    interrupts=path_audit(frames,edges,kinds,tuple(n for n in IRQ_ROOTS if enabled_target(n,features)))
    irq_max=max(p['foreground_bytes'] for p in interrupts.values())
    # Two full IRQ paths plus 256 bytes each for hardware exception/FPU/DCP/
    # alignment/dispatch frames. Current configured IRQ priorities do not nest;
    # reserving two levels is deliberate extra engineering headroom.
    irq_allowance=2*(irq_max+256)
    main=max(p['foreground_bytes'] for name,p in paths.items()
             if 'worker_main' not in name and name!='core1_wrapper')
    worker=max(p['foreground_bytes'] for name,p in paths.items()
               if 'worker_main' in name or name=='core1_wrapper')
    complete=all(p['complete'] for p in (*paths.values(),*interrupts.values()))
    symbols=read_symbols(str(objdump).replace('objdump','nm'),str(elf))
    main_reservation=symbols['__StackTop']-symbols['__StackBottom']
    worker_reservation=symbols['__StackOneTop']-symbols['__StackOneBottom']
    main_total=main+irq_allowance;worker_total=worker+irq_allowance
    return {'certified':False,'hardware_validated':False,
        'software_accounting_complete':complete,
        'software_gate_passed':complete and main_total<=main_reservation-2048 and worker_total<=worker_reservation-1024,
        'note':'Closed linked-image software accounting with explicit external ROM allowances. Physical ROM identity, stress watermarks and timing remain hardware validation gates; this is not a physical stack certificate.',
        'main':{'foreground_bytes':main,'with_interrupts_bytes':main_total,'reservation_bytes':main_reservation,'remaining_bytes':main_reservation-main_total,'required_margin_bytes':2048},
        'worker':{'foreground_bytes':worker,'with_interrupts_bytes':worker_total,'reservation_bytes':worker_reservation,'remaining_bytes':worker_reservation-worker_total,'required_margin_bytes':1024},
        'interrupt_allowance_bytes':irq_allowance,'interrupt_paths':interrupts,
        'external_allowances':{n:size for n,size in frames.items() if kinds[n]=='external-budget'},
        'callback_contracts':contracts,'linked_disassembly_sha256':digest,
        'features':features,
        'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
        'frame_count':len(frames),'edge_count':sum(map(len,edges.values())),'roots':paths}
