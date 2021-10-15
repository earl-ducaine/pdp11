







setxkbmap -layout "us+compose(menu)" -print
setxkbmap -layout "dvorak+compose(menu)" -print


xkb_keymap {
	xkb_keycodes  { include "evdev+aliases(qwerty)"	};
	xkb_types     { include "complete"	};
	xkb_compat    { include "complete"	};
	xkb_symbols   { include "pc+dvorak+compose(menu)+inet(evdev)"	};
	xkb_geometry  { include "pc(pc105)"	};
};

xkbcomp -w 10 \
	-I/home/rett/dev/common-lisp/its/xkeyboard-config \
	-R/usr/share/X11/xkb -xkm \
	~/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon/keymap-us.km \
	~/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon/us.xkm


xkbcomp -w 10 \
	-I/home/rett/dev/common-lisp/its/xkeyboard-config \
	-R/usr/share/X11/xkb \
	-xkm \
	keymap-dvorak.km \
	dvorak.xkm


xkbcomp -w 10 \
	-R. \
	-xkm \
	keymap-us.text \
	us.xkb

xkbcomp -w 10 \
	-R. \
	-xkm \
	keymap-us.text \
	dvorak.xkb
