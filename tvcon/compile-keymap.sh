



function compile-keymap {
    xkbcomp -w 10 -R/home/rett/dev/common-lisp/its/xkeyboard-config \
	    -xkm \
	    ~/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon/keymap.km \
	    ~/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon/dummy.km
}

compile-keymap
