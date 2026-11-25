"""Compatibility entry point for the live host/UI regression suite."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('test_live_host.py')), run_name='__main__')
