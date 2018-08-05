lisp: lisp.c
	cc -Wall -g3 -O0 -o lisp lisp.c

hamt: hamt.c
	cc -Wall -g3 -O0 -o hamt hamt.c

eav: eav.cpp
	c++ -Wall -g3 -O0 -o eav eav.cpp

bptree: bptree.cpp
	c++ -Wall -g3 -O0 -o bptree bptree.cpp
