mod-host
========

About
-----

mod-host is an LV2 host for JACK, controllable via socket or command line

Currently the host supports the following LV2 features:

* lv2core
* atom
* buf-size
* event
* log
* midi
* options
* parameters
* patch
* presets
* state
* time
* uri-map
* urid
* worker

mod-host is part of the [MOD project](http://moddevices.com).


Building
--------

mod-host uses a simple Makefile to build the source code.
The steps to build and install are:

    make
    make install

You can change the base installation path passing PREFIX as argument of make.

Dependencies:

    libjack-jackd2  >= 1.9.8
    liblilv         >= 0.14.2
    libreadline     >= 6.2
    lilv-utils      (optional)
    fftw            (optional)
    hylia           (optional)

On Debian-based Linux distributions (Ubuntu, Mint, etc) most of these can be installed with:

    sudo apt install libreadline-dev liblilv-dev lilv-utils libfftw3-dev libjack-jackd2-dev

For Hylia, please go to https://github.com/falkTX/Hylia to install from source.


Running
-------

For developing start it like this:

    $ ./mod-host -n -p 5555 -f 5556

mod-host does not startup JACK automatically, so you need to start it before
running mod-host.

If you run mod-host without options the process will be forked and it can only
be controlled through the socket.
The default socket port is 5555, this can be changed by passing the option
-p (or --socket-port) to mod-host.

The other way to control mod-host is the interactive mode, in this case the
commands must be provided on the shell prompt.
The interactive mode has autocomplete, therefore, you can always type `[TAB]`
twice any time you want a hint.

Note: When you are in the interactive mode, socket communication won't work.


Options
-------
    -v, --verbose
        verbose messages

    -p, --socket-port=<port>
        socket port definition

    -f, --feedback-port<port>
        feedback port definition

    -i, --interactive
        interactive shell mode

    -n, --nofork 
        run in non-forking mode

    -V, --version
        print program version and exit

    -h, --help
        print help and exit


Commands (or Protocol)
----------------------

The commands supported by mod-host are:

    add <lv2_uri> <instance_number>
        * add an LV2 plugin encapsulated as a jack client
        e.g.: add http://lv2plug.in/plugins/eg-amp 0
        instance_number must be any value between 0 ~ 9999, inclusively

    remove <instance_number>
        * remove an LV2 plugin instance (and also the jack client)
        e.g.: remove 0

    preset_load <instance_number> <preset_uri>
        * load a preset state to given effect instance
        e.g.: preset_load 0 "http://drobilla.net/plugins/mda/presets#JX10-moogcury-lite"

    preset_save <instance_number> <preset_name> <dir> <file_name>
        * save a preset state from given effect instance
        e.g.: preset_save 0 "My Preset" /home/user/.lv2/my-presets.lv2 mypreset.ttl

    preset_show <instance_number> <preset_uri>
        * show the preset information of requested instance / URI
        e.g.: preset_show 0 http://drobilla.net/plugins/mda/presets#EPiano-bright

    connect <origin_port> <destination_port>
        * connect two effect audio ports
        e.g.: connect system:capture_1 effect_0:in

    disconnect <origin_port> <destination_port>
        * disconnect two effect audio ports
        e.g.: disconnect system:capture_1 effect_0:in

    bypass <instance_number> <bypass_value>
        * toggle effect processing
        e.g.: bypass 0 1
        if bypass_value = 1 bypass effect
        if bypass_value = 0 process effect

    param_set <instance_number> <param_symbol> <param_value>
        * set a value to given control
        e.g.: param_set 0 gain 2.50

    param_get <instance_number> <param_symbol>
        * get the value of the request control
        e.g.: param_get 0 gain

    param_monitor <instance_number> <param_symbol> <cond_op> <value>
        * do monitoring a effect instance control port according given condition
        e.g: param_monitor 0 gain > 2.50

    licensee <instance_number>
        * get the licensee name for a commercial plugin
        e.g.: licensee 0

    monitor <addr> <port> <status>
        * open a socket port to monitoring parameters
        e.g: monitor localhost 12345 1
        if status = 1 start monitoring
        if status = 0 stop monitoring

    monitor_output <instance_number> <param_symbol>
        * request monitoring of an output control port in the feedback port
        e.g.: monitor_output 0 meter

    midi_learn <instance_number> <param_symbol> <minimum> <maximum>
        * start MIDI learn for a parameter
        e.g.: midi_learn 0 gain 0.0 1.0

    midi_map <instance_number> <param_symbol> <midi_channel> <midi_cc> <minimum> <maximum>
        * map a MIDI controller to a parameter
        e.g.: midi_map 0 gain 0 7 0.0 1.0

    midi_unmap <instance_number> <param_symbol>
        * unmap the MIDI controller from a parameter
        e.g.: unmap 0 gain

    set_midi_program_change_pedalboard_bank_channel <enable> <midi_channel>
        * set the MIDI channel which changes pedalboard banks on MIDI program change. <midi_channel> is in the range of [0,15].
        e.g.: set_midi_program_change_pedalboard_bank_channel 1 5 to enable listening for bank changes on channel 6

    set_midi_program_change_pedalboard_snapshot_channel <enable> <midi_channel>
        * set the MIDI channel which changes pedalboard snapshots on MIDI program change. <midi_channel> is in the range of [0,15].
        e.g.: set_midi_program_change_pedalboard_snapshot_channel 1 4 to enable listening for preset changes on channel 5

    cc_map <instance_number> <param_symbol> <device_id> <actuator_id> <label> <value> <minimum> <maximum> <steps> <unit> <scalepoints_count> <scalepoints...>
        * map a Control Chain actuator to a parameter
        e.g.: cc_map 0 gain 0 1 "Gain" 0.0 -24.0 3.0 33 "dB" 0

    cc_unmap <instance_number> <param_symbol>
        * unmap the Control Chain actuator from a parameter
        e.g.: unmap 0 gain

    cv_map <instance_number> <param_symbol> <source_port_name> <minimum> <maximum> <operational-mode>
        * map a CV source port to a parameter, operational-mode being one of '-', '+', 'b' or '='
        e.g.: cv_map 0 gain "AMS CV Source:CV Out 1" -24.0 3.0 =

    cv_unmap <instance_number> <param_symbol>
        * unmap the CV source port actuator from a parameter
        e.g.: cv_unmap 0 gain

    cpu_load
        * return current jack cpu load

    load <file_name>
        * load a history command file
        * dummy way to save/load workspace state
        e.g.: load my_setup

    save <file_name>
        * saves the history of typed commands
        * dummy way to save/load workspace state
        e.g.: save my_setup

    bundle_add <bundle_path>
        * add a bundle to the running lv2 world
        e.g.: bundle_add /path/to/bundle.lv2

    bundle_remove <bundle_path>
        * remove a bundle from the running lv2 world
        e.g.: bundle_remove /path/to/bundle.lv2

    feature_enable <feature> <enable>
        * enable or disable a feature
        e.g.: feature_enable link 1
        current features are "link", "processing" and "midi_clock_slave"

    transport <rolling> <beats_per_bar> <beats_per_minute>
        * change the current transport state
        e.g.: transport 1 4 120

    output_data_ready
        * report feedback port ready for more messages

    help
        * show a help message

    quit
        bye!

For each effect added one client on JACK will be created. The names of clients
follow the standard: effect_\<instance_number\>

If a valid command is executed a response is given as following:

    resp <status> [value]

If status is a negative number an error has occurred. The table below shows the number of each error.

| status  | error                            |
| --------|----------------------------------|
| -1      | ERR\_INSTANCE\_INVALID           |
| -2      | ERR\_INSTANCE\_ALREADY\_EXISTS   |
| -3      | ERR\_INSTANCE\_NON\_EXISTS       |
| -4      | ERR\_INSTANCE\_UNLICENSED        |
| -101    | ERR\_LV2\_INVALID\_URI           |
| -102    | ERR\_LV2\_INSTANTIATION          |
| -103    | ERR\_LV2\_INVALID\_PARAM\_SYMBOL |
| -104    | ERR\_LV2\_INVALID\_PRESET\_URI   |
| -105    | ERR\_LV2\_CANT\_LOAD\_STATE      |
| -201    | ERR\_JACK\_CLIENT\_CREATION      |
| -202    | ERR\_JACK\_CLIENT\_ACTIVATION    |
| -203    | ERR\_JACK\_CLIENT\_DEACTIVATION  |
| -204    | ERR\_JACK\_PORT\_REGISTER        |
| -205    | ERR\_JACK\_PORT\_CONNECTION      |
| -206    | ERR\_JACK\_PORT\_DISCONNECTION   |
| -301    | ERR\_ASSIGNMENT\_ALREADY\_EXISTS |
| -302    | ERR\_ASSIGNMENT\_INVALID\_OP     |
| -303    | ERR\_ASSIGNMENT\_LIST\_FULL      |
| -304    | ERR\_ASSIGNMENT\_FAILED          |
| -401    | ERR\_CONTROL\_CHAIN\_UNAVAILABLE |
| -402    | ERR\_LINK\_UNAVAILABLE           |
| -901    | ERR\_MEMORY\_ALLOCATION          |
| -902    | ERR\_INVALID\_OPERATION          |

A status zero or positive means that the command was executed successfully.
In case of the add command, the status returned is the instance number.
The value field currently only exists for the param_get command.
