#ifndef SDL2_KEYMAP_EXTRAKEYS_H_
#define SDL2_KEYMAP_EXTRAKEYS_H_

struct sdl2_keymap{
	int code;
	const char* key;
	sdl2_keymap(int code, const char* key): code(code), key(key) {}
};

static const sdl2_keymap sdl2_keymap_extrakeys[] = {
		//x wheel, sdl2 only
		sdl2_keymap(-35, "WHEELXR"),
		sdl2_keymap(-36, "WHEELXL"),
		//extended keys and modifiers
		sdl2_keymap(0x40000059, "KP1"),
		sdl2_keymap(0x4000005a, "KP2"),
		sdl2_keymap(0x4000005b, "KP3"),
		sdl2_keymap(0x4000005c, "KP4"),
		sdl2_keymap(0x4000005d, "KP5"),
		sdl2_keymap(0x4000005e, "KP6"),
		sdl2_keymap(0x4000005f, "KP7"),
		sdl2_keymap(0x40000060, "KP8"),
		sdl2_keymap(0x40000061, "KP9"),
		sdl2_keymap(0x40000062, "KP0"),
		sdl2_keymap(0x40000063, "KP_PERIOD"),
		sdl2_keymap(0x40000054, "KP_DIVIDE"),
		sdl2_keymap(0x40000055, "KP_MULTIPLY"),
		sdl2_keymap(0x40000056, "KP_MINUS"),
		sdl2_keymap(0x40000057, "KP_PLUS"),
		sdl2_keymap(0x40000058, "KP_ENTER"),
		sdl2_keymap(0x40000067, "KP_EQUALS"),
		sdl2_keymap(0x40000052, "UP"),
		sdl2_keymap(0x40000051, "DOWN"),
		sdl2_keymap(0x4000004f, "RIGHT"),
		sdl2_keymap(0x40000050, "LEFT"),
		sdl2_keymap(0x40000048, "PAUSE"),
		sdl2_keymap(0x40000049, "INSERT"),
		sdl2_keymap(0x4000004a, "HOME"),
		sdl2_keymap(0x4000004d, "END"),
		sdl2_keymap(0x4000004b, "PAGEUP"),
		sdl2_keymap(0x4000004e, "PAGEDOWN"),
		sdl2_keymap(0x4000003a, "F1"),
		sdl2_keymap(0x4000003b, "F2"),
		sdl2_keymap(0x4000003c, "F3"),
		sdl2_keymap(0x4000003d, "F4"),
		sdl2_keymap(0x4000003e, "F5"),
		sdl2_keymap(0x4000003f, "F6"),
		sdl2_keymap(0x40000040, "F7"),
		sdl2_keymap(0x40000041, "F8"),
		sdl2_keymap(0x40000042, "F9"),
		sdl2_keymap(0x40000043, "F10"),
		sdl2_keymap(0x40000044, "F11"),
		sdl2_keymap(0x40000045, "F12"),
		sdl2_keymap(0x40000053, "NUMLOCK"),
		sdl2_keymap(0x40000039, "CAPSLOCK"),
		sdl2_keymap(0x40000047, "SCROLLOCK"),
		sdl2_keymap(0x400000e5, "RSHIFT"),
		sdl2_keymap(0x400000e1, "LSHIFT"),
		sdl2_keymap(0x400000e4, "RCTRL"),
		sdl2_keymap(0x400000e0, "LCTRL"),
		sdl2_keymap(0x400000e6, "RALT"),
		sdl2_keymap(0x400000e2, "LALT"),
		sdl2_keymap(0x400000e7, "RMETA"),
		sdl2_keymap(0x400000e3, "LMETA"),
		sdl2_keymap(0x40000065, "COMPOSE"),
		sdl2_keymap(0x40000075, "HELP"),
		sdl2_keymap(0x40000046, "PRINT"),
		sdl2_keymap(0x4000009a, "SYSREQ"),
		sdl2_keymap(0x40000076, "MENU"),
                // gamepad map
                sdl2_keymap(0xFF000001, "GP_LT"),
                sdl2_keymap(0xFF000002, "GP_RT"),
                sdl2_keymap(0xFF000003, "GP_LB"),
                sdl2_keymap(0xFF000004, "GP_RB"),
                sdl2_keymap(0xFF000005, "GP_A"),
                sdl2_keymap(0xFF000006, "GP_B"),
                sdl2_keymap(0xFF000007, "GP_X"),
                sdl2_keymap(0xFF000008, "GP_Y"),
                sdl2_keymap(0xFF000009, "GP_START"),
                sdl2_keymap(0xFF00000a, "GP_BACK"),
                sdl2_keymap(0xFF00000b, "GP_GUIDE"),
                sdl2_keymap(0xFF00000c, "GP_LS"),
                sdl2_keymap(0xFF00000d, "GP_RS"),
                sdl2_keymap(0xFF00000e, "GP_UP"),
                sdl2_keymap(0xFF00000f, "GP_RIGHT"),
                sdl2_keymap(0xFF000010, "GP_DOWN"),
                sdl2_keymap(0xFF000011, "GP_LEFT"),
                sdl2_keymap(0xFF000012, "GP_DUP"),
                sdl2_keymap(0xFF000013, "GP_DRIGHT"),
                sdl2_keymap(0xFF000014, "GP_DDOWN"),
                sdl2_keymap(0xFF000015, "GP_DLEFT")
};

// gamepad related data
static struct ShortNames {
    hashtable<const char*, const char*> names;
    hashtable<const char*, int> codes;
    ShortNames() {
        names["lefttrigger"] = "GP_LT";
        names["righttrigger"] = "GP_RT";
        names["leftshoulder"] = "GP_LB";
        names["rightshoulder"] = "GP_RB";
        names["a"] = "GP_A";
        names["b"] = "GP_B";
        names["x"] = "GP_X";
        names["y"] = "GP_Y";
        names["start"] = "GP_START";
        names["back"] = "GP_BACK";
        names["guide"] = "GP_GUIDE";
        names["leftstick"] = "GP_LS";
        names["rightstick"] = "GP_RS";
        names["spup"] = "GP_UP";
        names["spright"] = "GP_RIGHT";
        names["spdown"] = "GP_DOWN";
        names["spleft"] = "GP_LEFT";
        names["dpup"] = "GP_DUP";
        names["dpright"] = "GP_DRIGHT";
        names["dpdown"] = "GP_DDOWN";
        names["dpleft"] = "GP_DLEFT";
        codes["lefttrigger"] = 0xFF000001;
        codes["righttrigger"] = 0xFF000002;
        codes["leftshoulder"] = 0xFF000003;
        codes["rightshoulder"] = 0xFF000004;
        codes["a"] = 0xFF000005;
        codes["b"] = 0xFF000006;
        codes["x"] = 0xFF000007;
        codes["y"] = 0xFF000008;
        codes["start"] = 0xFF000009;
        codes["back"] = 0xFF00000a;
        codes["guide"] = 0xFF00000b;
        codes["leftstick"] = 0xFF00000c;
        codes["rightstick"] = 0xFF00000d;
        codes["spup"] = 0xFF00000e;
        codes["spright"] = 0xFF00000f;
        codes["spdown"] = 0xFF000010;
        codes["spleft"] = 0xFF000011;
        codes["dpup"] = 0xFF000012;
        codes["dpright"] = 0xFF000013;
        codes["dpdown"] = 0xFF000014;
        codes["dpleft"] = 0xFF000015;
    }
    char* getshortname(const char* name) {
        return (char*)names.find(name,NULL);
    }
    int getcode(const char* name) {
        return codes.find(name, 0);
    }
} padbuttons;


#endif /* SDL2_KEYMAP_EXTRAKEYS_H_ */
