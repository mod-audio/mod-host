
#include <stdio.h>
#include <unistd.h>
#include "../src/effects.h"

// valgrind --leak-check=full --show-reachable=yes --log-file=valgrind.log ./effectlib_test

int main (void)
{
    float value;
    int ret;
    const char *action;

    if (effects_init(NULL) == 0)
    {
        action = "add: http://lv2plug.in/plugins/eg-amp";
        ret = effects_add("http://lv2plug.in/plugins/eg-amp", 0, NULL);
        printf("%s, ret: %i\n", action, ret);

        if (ret != 0)
        {
            effects_finish(1);
            return 1;
        }

        action = "connect system:capture_1 effect_0:in";
        ret = effects_connect("system:capture_1", "effect_0:in");
        printf("%s, ret: %i\n", action, ret);

        action = "connect effect_0:out system:playback_1";
        ret = effects_connect("effect_0:out", "system:playback_1");
        printf("%s, ret: %i\n", action, ret);

        action = "set parameter gain";
        ret = effects_set_parameter(0, "gain", 10.0);
        printf("%s, ret: %i\n", action, ret);

        action = "get parameter gain";
        ret = effects_get_parameter(0, "gain", &value);
        printf("%s, ret: %i\n", action, ret);
        printf("value: %f\n", value);

        sleep(5);

        action = "bypass";
        ret = effects_bypass(0, 1);
        printf("%s, ret: %i\n", action, ret);

        sleep(5);

        action = "disconnect effect_0:out system:playback_1";
        ret = effects_disconnect("effect_0:out", "system:playback_1");
        printf("%s, ret: %i\n", action, ret);

        action = "disconnect system:capture_1 effect_0:in";
        ret = effects_disconnect("system:capture_1", "effect_0:in");
        printf("%s, ret: %i\n", action, ret);

        action = "remove 0";
        ret = effects_remove(0);
        printf("%s, ret: %i\n", action, ret);

        effects_finish(1);
    }

    return 0;
}
