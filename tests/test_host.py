#!/usr/bin/python2

from hosttest import s, load_egamp

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

