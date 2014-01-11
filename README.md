![alt text](https://dl.dropboxusercontent.com/u/98438890/mod-logo.png "MOD")

mod-host
========

About
-----

mod-host is a LV2 host for jackd, that can be controlled via a socket or shell.

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

mod-host is part of the [MOD project](http://portalmod.com).


Build
-----

mod-host uses a simple Makefile to build the source code.
The steps to build and install are:

    make
    sudo make install

The default instalation path is /usr/local/bin, this can be modified passing the variable INSTALL_PATH to make install, e.g.:

    sudo make install INSTALL_PATH=/usr/bin

Dependencies:

    libjack-jackd2  >= 1.9.8
    liblilv         >= 0.14.2
    libargtable2    >= 2.13
    libreadline     >= 6.2
    lilv-utils      (optional)

To turn doc/man.txt into a groff manpage you need txt2man. To build and install the manpage run:

    make man
    sudo make install-man

To read the manual type `man mod-host` in your terminal.

Run
---

mod-host does not startup jackd automatically, so you need to start it before
run mod-host.

If you run mod-host without parameters the process will be forked and can only
be controlled through the socket.
The default socket port is 5555, this can be changed by passing the parameter
-p (or --socket-port) to mod-host.

The other way to control mod-host is the interactive mode, in this case the
commands must be provided on the shell prompt.
The interactive mode has autocomplete, therefore, you can always type `[TAB]`
twice if you need a hint.

Obs.: When you are in the interactive mode, the socket communication does not work.

Commands (or Protocol)
----------------------

The commands supported by mod-host are:

    add <lv2_uri> <instance_number>
        This command adds a lv2 effect to pedalboard (jack session)
        e.g.: add http://lv2plug.in/plugins/eg-amp 0
        instance_number must be any value between 0 ~ 9999, inclusively

    remove <instance_number>
        This command removes a lv2 effect from pedalboard
        e.g.: remove 0

    connect <origin_port> <destination_port>
        This command connects two ports of effects, hardware or MIDI
        e.g.: connect system:capture_1 effect_0:in

    disconnect <origin_port> <destination_port>
        This command disconnects two ports of effects, hardware or MIDI
        e.g.: disconnect system:capture_1 effect_0:in

    param_set <instance_number> <param_symbol> <param_value>
        This command change the value of a parameter
        e.g.: param_set 0 gain 2.50

    param_get <instance_number> <param_symbol>
        This command show the value of a parameter
        e.g.: param_get 0 gain

    param_monitor <instance_number> <param_symbol> <cond_op> <value>
        This command defines a parameter to be monitored
        e.g: param_monitor 0 gain > 2.50

    monitor <addr> <port> <status>
        This command controls the monitoring of parameters
        e.g: monitor localhost 12345 1
        if status = 1 start monitoring
        if status = 0 stop monitoring

    map <instance_number> <param_symbol>
        This command maps a MIDI controller to control a parameter
        e.g.: map 0 gain

    unmap <instance_number> <param_symbol>
        This command unmaps a MIDI controller
        e.g.: unmap 0 gain

    bypass <instance_number> <bypass_value>
        This command process or bypass an effect
        e.g.: bypass 0 1
        if bypass_value = 1 bypass the effect
        if bypass_value = 0 process the effect

    load <filename>
        This command loads the history of typed commands
        e.g.: load my_preset

    save <filename>
        This command saves the history of typed commands
        e.g.: save my_preset

    help
        This command show a help message

    quit
        bye!

For each effect added one client on jackd will be created. The names of clients
follow the standard: effect_\<instance_number\>

For each command sent one response is given. If the command is valid the
response format will be:

    resp <status> [value]

If status is a negative number, an error occurred. The error will be one of the
following:

| status  | error                            |
| --------|----------------------------------|
| -1      | ERR_INSTANCE_INVALID             |
| -2      | ERR_INSTANCE_ALREADY_EXISTS      |
| -3      | ERR_INSTANCE_NON_EXISTS          |
| -101    | ERR_LV2_INVALID_URI              |
| -102    | ERR_LILV_INSTANTIATION           |
| -103    | ERR_LV2_INVALID_PARAM_SYMBOL     |
| -201    | ERR_JACK_CLIENT_CREATION         |
| -202    | ERR_JACK_CLIENT_ACTIVATION       |
| -203    | ERR_JACK_CLIENT_DEACTIVATION     |
| -204    | ERR_JACK_PORT_REGISTER           |
| -205    | ERR_JACK_PORT_CONNECTION         |
| -206    | ERR_JACK_PORT_DISCONNECTION      |
| -301    | ERR_MIDI_ASSIGNMENT_LIST_IS_FULL |
| -302    | ERR_MIDI_PARAM_NOT_FOUND         |
| -901    | ERR_MEMORY_ALLOCATION            |

A status zero or positive means that the command was executed successfully.
In case of the add command, the status returned is the instance number.
The value field currently only exists for the param_get command.

