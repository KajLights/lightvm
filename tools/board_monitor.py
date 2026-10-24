"""Opt-in, same-connection temperature guard for hardware benchmark clients.

Records native sensor readings without opening a competing serial connection.
The limit is an operator test-stop threshold, not a rated chip temperature.
"""
from datetime import datetime, timezone
import json
import math
import time


class BoardMonitor:
    def __init__(self, client, path, limit_c=80, interval=2):
        if not math.isfinite(limit_c) or interval <= 0:
            raise ValueError('invalid temperature monitor settings')
        self.client, self.path, self.limit_c, self.interval = client, path, limit_c, interval
        self.next_read, self.busy, self.failure = 0, False, None
        self.command = client.command
        client.command = self.guarded_command
        client.board_monitor = self

    def record(self, row):
        with self.path.open('a') as stream:
            stream.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(),
                                     'monotonic': time.monotonic(), **row}, allow_nan=False) + '\n')

    def guarded_command(self, *args, **kwargs):
        if not self.busy:
            self.check()
        return self.command(*args, **kwargs)

    def check(self, force=False):
        if self.failure:
            raise RuntimeError(self.failure)
        if self.busy or not force and time.monotonic() < self.next_read:
            return
        self.busy = True
        saved = bytes(self.client.data_reply), bytes(self.client.pixel_snapshot)
        try:
            self.client.manage('input_list')
            sensors = json.loads(self.client.data_reply)
            temperature = next((s for s in sensors if s['name'] == 'board.temperature_c'), {})
            value, age = temperature.get('value'), temperature.get('age_ms')
            valid = (temperature.get('valid') and isinstance(value, (int, float))
                     and math.isfinite(value) and isinstance(age, (int, float)) and 0 <= age <= 2000)
            self.record({'sensors': sensors, 'limit_c': self.limit_c})
            if not valid:
                raise RuntimeError('temperature sensor unavailable or stale')
            if value >= self.limit_c:
                raise RuntimeError(f'temperature {value:.1f} C reached test stop threshold {self.limit_c:g} C')
            self.next_read = time.monotonic() + self.interval
        except Exception as error:
            self.failure = 'Board monitor stopped test: ' + str(error)
            stop = {'failure': self.failure}
            try:
                self.client.configure({'schema': 'lightvm.config', 'outputs': []})
                stop['outputs_cleared'] = True
            except Exception as stop_error:
                stop['outputs_cleared'] = False
                stop['stop_error'] = str(stop_error)
            self.record(stop)
            raise RuntimeError(self.failure) from error
        finally:
            self.client.data_reply[:] = saved[0]
            self.client.pixel_snapshot[:] = saved[1]
            self.busy = False

    def sleep(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.check()
            time.sleep(min(.5, max(0, deadline - time.monotonic())))


def add_arguments(parser):
    from pathlib import Path
    parser.add_argument('--temperature-log', type=Path,
                        help='enable native temperature guard and append JSONL samples')
    parser.add_argument('--temperature-limit', type=float, default=80,
                        help='test stop threshold in C (default 80; not a chip rating)')


def attach(client, args):
    if getattr(args, 'temperature_log', None):
        return BoardMonitor(client, args.temperature_log, args.temperature_limit)
    return None


def sleep(client, seconds):
    monitor = getattr(client, 'board_monitor', None)
    if monitor:
        monitor.sleep(seconds)
    else:
        time.sleep(seconds)
