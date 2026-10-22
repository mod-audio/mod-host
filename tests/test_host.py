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
    r = load_egamp()
    assert int(r[0]) == 0
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
    r = load_egamp()
    assert int(r[0]) == 0
    r = s('preset_load 0 urn:test:Gain3')
    assert int(r[0]) == 0
    r = s("param_get 0 gain")
    assert int(r[0]) == 0
    assert float(r[1]) == 3.0

def test_preset_save():
    r = load_egamp()
    assert int(r[0]) == 0
    r = s('param_set 0 gain 5.0')
    assert int(r[0]) == 0

    r = s("preset_save 0 Gain5 /tmp/lv2path/presets.lv2 gain_5.ttl")
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

    r = s('preset_load 0 file:///tmp/lv2path/presets.lv2/gain_5.ttl')
    assert int(r[0]) == 0

    r = s('param_get 0 gain')
    assert int(r[0]) == 0
    assert float(r[1]) == 5.0

def test_add_without_name_uses_effect_prefix():
    r = s('add http://lv2plug.in/plugins/eg-amp 0')
    assert int(r[0]) == 0
    names = jack_client_names()
    assert 'effect_0' in names
    r = s('remove 0')
    assert int(r[0]) == 0

def test_add_with_name_uses_the_requested_jack_client_name():
    r = s('add http://lv2plug.in/plugins/eg-amp 0 channel1-eg-amp')
    assert int(r[0]) == 0
    names = jack_client_names()
    assert 'channel1-eg-amp' in names
    assert 'effect_0' not in names
    r = s('remove 0')
    assert int(r[0]) == 0

def test_add_with_name_sanitises_colon():
    # ':' is jack's client:port separator; a name carrying one must be replaced, not
    # passed straight through (it would make every one of the client's own ports
    # unparseable as "client:port").
    r = s('add http://lv2plug.in/plugins/eg-amp 0 bad:name')
    assert int(r[0]) == 0
    names = jack_client_names()
    assert 'bad_name' in names
    assert 'bad:name' not in names
    r = s('remove 0')
    assert int(r[0]) == 0

def test_add_with_colliding_name_appends_the_instance_number():
    r = s('add http://lv2plug.in/plugins/eg-amp 0 shared-name')
    assert int(r[0]) == 0
    r = s('add http://lv2plug.in/plugins/eg-amp 1 shared-name')
    assert int(r[0]) == 1
    names = jack_client_names()
    assert 'shared-name' in names
    assert 'shared-name_1' in names
    r = s('remove 0')
    assert int(r[0]) == 0
    r = s('remove 1')
    assert int(r[0]) == 0

def test_add_still_accepts_the_old_three_token_form():
    # Protocol backwards-compatibility: a caller that never learned about the optional
    # 4th token must keep working exactly as before.
    r = s('add http://lv2plug.in/plugins/eg-amp 0')
    assert int(r[0]) == 0
    r = s('remove 0')
    assert int(r[0]) == 0



def test_plugin_info_of_an_lv2_port():
    r = load_egamp()
    assert int(r[0]) == 0
    assert s("param_info 0 gain") == ["0", "db", "linear", "-90", "24", "0", "0", "gain"]
    assert s("param_info 0 out") == ["-103"]
    assert s("param_info 0 :bypass") == ["-103"]
    assert s("param_info 1 gain") == ["-3"]
    assert s("remote_pages 0") == ["0"]
    assert s("remote_page_get 0 0") == ["-902"]
    assert s('track_info 0 "Kick In" #FF8000') == ["-902"]
