lisp: lisp.c
	cc -Wall -g3 -O0 -o lisp lisp.c

hamt: hamt.c
	cc -Wall -g3 -O0 -o hamt hamt.c

eav: eav.c
	cc -Wall -g3 -O0 -o eav eav.c
	./eav | dot -Tpng -o eav.png
