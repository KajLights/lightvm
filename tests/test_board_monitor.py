import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from board_monitor import BoardMonitor
from client import Client, SerialTransport

class FakeClient:
    def __init__(self):
        self.data_reply=bytearray(b'pending reply');self.pixel_snapshot=bytearray(b'pixels')
        self.sensors=[{'name':'board.temperature_c','valid':True,'value':60,'age_ms':10}]
        self.reads=0;self.calls=[];self.cleared=False
    def command(self,*args,**kwargs):self.calls.append(args);return 'ok'
    def manage(self,op,**fields):
        self.command(op);self.reads+=1;self.data_reply[:]=json.dumps(self.sensors).encode()
    def configure(self,value):self.command('config');self.cleared=value['outputs']==[]

class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.client=FakeClient();self.path=Path(self.temp.name)/'temperature.jsonl'
        self.monitor=BoardMonitor(self.client,self.path)
    def test_sampling_keeps_pending_reply_and_rate_limit(self):
        self.client.command('status');self.client.command('status')
        self.assertEqual(self.client.reads,1)
        self.assertEqual(self.client.data_reply,b'pending reply')
        self.assertEqual(self.client.pixel_snapshot,b'pixels')
        self.assertEqual(json.loads(self.path.read_text())['sensors'][0]['value'],60)
    def test_hot_invalid_missing_and_stale_readings_stop_and_latch(self):
        for change in [{'value':80},{'valid':False},{'age_ms':3000},{'value':None},{'value':float('nan')}]:
            with self.subTest(change=change):
                c=FakeClient();c.sensors[0].update(change);m=BoardMonitor(c,self.path)
                with self.assertRaises(RuntimeError):c.command('play')
                self.assertTrue(c.cleared);self.assertNotIn(('play',),c.calls)
                c.sensors[0].update(value=60,valid=True,age_ms=0)
                with self.assertRaises(RuntimeError):c.command('play')
    def test_command_failure_attempts_stop_and_records_failure(self):
        with patch.object(self.client,'manage',side_effect=RuntimeError('USB failure')):
            with self.assertRaisesRegex(RuntimeError,'USB failure'):self.client.command('play')
        self.assertTrue(self.client.cleared)
        self.assertTrue(json.loads(self.path.read_text())['outputs_cleared'])
    def test_latched_failure_still_closes_serial_transport(self):
        transport=object.__new__(SerialTransport)
        class Port:
            closed=False
            def close(self):self.closed=True
        transport.serial=Port()
        client=Client(transport);client.connections=True
        monitor=BoardMonitor(client,self.path);monitor.failure='already stopped'
        client.close()
        self.assertTrue(transport.serial.closed)

if __name__=='__main__':unittest.main()
