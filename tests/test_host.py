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

def test_preset():
    r = s("add http://lv2plug.in/plugins/eg-amp 0")
    assert int(r[0]) == 0
    r = s('preset 0 "Gain 3"')
    assert int(r[0]) == 0
    r = s("param_get 0 gain")
    assert int(r[0]) == 0
    assert float(r[1]) == 3.0
