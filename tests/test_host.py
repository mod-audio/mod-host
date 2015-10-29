#!/usr/bin/python2

from hosttest import *
import time

def setup_function(function):
    reset()
    connect_socket()

def teardown_function(function):
    kill_host()
    kill_jack()
    reset_socket()

def test_load_gain_change_param():
    load_egamp()
    r = s("param_get 0 gain")
    assert int(r[0]) == 0
    assert float(r[1]) == 0.0
    r = s("param_set 0 gain 5.0")
    assert int(r[0]) == 0
    r = s("param_get 0 gain")
    assert int(r[0]) == 0
    assert float(r[1]) == 5.0
    r = s("remove 0")
    assert int(r[0]) == 0

def test_preset_load():
    load_egamp()
    r = s('preset 0 "Gain 3"')
    assert int(r[0]) == 0
    r = s("param_get 0 gain")
    assert int(r[0]) == 0
    assert float(r[1]) == 3.0

def test_preset_save():
    load_egamp()
    r = s('param_set 0 gain 5.0')
    assert int(r[0]) == 0

    r = s("save_preset 0 Gain5 /tmp/lv2path/presets.lv2 gain_5.ttl")
    assert int(r[0]) == 0

    # restarting the mod-host to load the saved preset
    kill_host()
    run_host()
    reset_socket()
    time.sleep(.3)
    connect_socket()

    load_egamp()
    r = s('param_get 0 gain')
    assert int(r[0]) == 0
    assert float(r[1]) == 0.0

    r = s('preset 0 file:///tmp/lv2path/presets.lv2/gain_5.ttl')
    assert int(r[0]) == 0

    r = s('param_get 0 gain')
    assert int(r[0]) == 0
    assert float(r[1]) == 5.0


