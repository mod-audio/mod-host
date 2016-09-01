mod-host
========

About
-----

mod-host is an LV2 host for JACK, controllable via socket or command line

Currently the host supports the following LV2 features:

* lv2core
* atom
* event
* buf-size
* midi
* options
* uri-map
* urid
* worker
* presets

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


Running
-------

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

    -i, --interactive
        interactive shell mode

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

    connect <origin_port> <destination_port>
        * connect two effect audio ports
        e.g.: connect system:capture_1 effect_0:in

    disconnect <origin_port> <destination_port>
        * disconnect two effect audio ports
        e.g.: disconnect system:capture_1 effect_0:in

    preset_load <instance_number> <preset_uri>
        * load a preset state to given effect instance
        e.g.: preset_load 0 "http://drobilla.net/plugins/mda/presets#JX10-moogcury-lite"

    preset_save <instance_number> <preset_name> <dir> <file_name>
        * save a preset state from given effect instance
        e.g.: preset_save 0 "My Preset" /home/user/.lv2/my-presets.lv2 mypreset.ttl

    preset_show <instance_number> <preset_uri>
        * show the preset information of requested instance / URI
        e.g.: preset_show 0 http://drobilla.net/plugins/mda/presets#EPiano-bright

    param_list <instance_number>
        * list the available controls for an instance
        e.g.: param_list 0

    param_set <instance_number> <param_symbol> <param_value>
        * set a value to given control
        e.g.: param_set 0 gain 2.50

    param_get <instance_number> <param_symbol>
        * get the value of the request control
        e.g.: param_get 0 gain

    param_monitor <instance_number> <param_symbol> <cond_op> <value>
        * do monitoring a effect instance control port according given condition
        e.g: param_monitor 0 gain > 2.50

    monitor <addr> <port> <status>
        * open a socket port to monitoring parameters
        e.g: monitor localhost 12345 1
        if status = 1 start monitoring
        if status = 0 stop monitoring

    midi_learn <instance_number> <param_symbol>
        This command maps starts MIDI learn for a parameter
        e.g.: midi_learn 0 gain

    midi_map <instance_number> <param_symbol> <midi_channel> <midi_cc>
        This command maps a MIDI controller to a parameter
        e.g.: midi_map 0 gain 0 7

    midi_unmap <instance_number> <param_symbol>
        This command unmaps the MIDI controller from a parameter
        e.g.: unmap 0 gain

    bypass <instance_number> <bypass_value>
        * toggle effect processing
        e.g.: bypass 0 1
        if bypass_value = 1 bypass effect
        if bypass_value = 0 process effect

    load <file_name>
        * load a history command file
        * dummy way to save/load workspace state
        e.g.: load my_setup

    save <file_name>
        * saves the history of typed commands
        * dummy way to save/load workspace state
        e.g.: save my_setup

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
| -301    | ERR\_MEMORY\_ALLOCATION          |

A status zero or positive means that the command was executed successfully.
In case of the add command, the status returned is the instance number.
The value field currently only exists for the param_get command.
