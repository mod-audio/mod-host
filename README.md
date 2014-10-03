mod-host
========

About
-----

mod-host is a LV2 host for JACK, controllable via socket or command line

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

mod-host is part of the [MOD project](http://portalmod.com).


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

Note: When you are in the interactive mode, the socket communication won't work.


Options
-------
    -v, --verbose
        verbose messages

    -p, --socket-port=<port>
        socket port definition

    -i, --interactive
        interactive shell mode

    -h, --help
        print help and exit


Commands (or Protocol)
----------------------

The commands supported by mod-host are:

    add <lv2_uri> <instance_number>
        e.g.: add http://lv2plug.in/plugins/eg-amp 0
        instance_number must be any value between 0 ~ 9999, inclusively

    remove <instance_number>
        e.g.: remove 0

    connect <origin_port> <destination_port>
        e.g.: connect system:capture_1 effect_0:in

    disconnect <origin_port> <destination_port>
        e.g.: disconnect system:capture_1 effect_0:in

    preset <instance_number> <preset_name>
        e.g.: preset 0 "Invert CC Value"

    param_set <instance_number> <param_symbol> <param_value>
        e.g.: param_set 0 gain 2.50

    param_get <instance_number> <param_symbol>
        e.g.: param_get 0 gain

    param_monitor <instance_number> <param_symbol> <cond_op> <value>
        e.g: param_monitor 0 gain > 2.50

    monitor <addr> <port> <status>
        e.g: monitor localhost 12345 1
        if status = 1 start monitoring
        if status = 0 stop monitoring

    bypass <instance_number> <bypass_value>
        e.g.: bypass 0 1
        if bypass_value = 1 bypass the effect
        if bypass_value = 0 process the effect

    load <filename>
        e.g.: load my_setup

    save <filename>
        e.g.: save my_setup
        this command saves the history of typed commands

    help
        show a help message

    quit
        bye!

For each effect added one client on JACK will be created. The names of clients
follow the standard: effect_\<instance_number\>

For each command sent one response is given. If the command is valid the
response format will be:

    resp <status> [value]

If status is a negative number, an error occurred. The error will be one of the
following:

| status  | error                            |
| --------|----------------------------------|
| -1      | ERR\_INSTANCE\_INVALID           |
| -2      | ERR\_INSTANCE\_ALREADY\_EXISTS   |
| -3      | ERR\_INSTANCE\_NON\_EXISTS       |
| -101    | ERR\_LV2\_INVALID\_URI           |
| -102    | ERR\_LILV\_INSTANTIATION         |
| -103    | ERR\_LV2\_INVALID\_PARAM\_SYMBOL |
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
