procx: procx.c
	gcc -o procx procx.c

run: procx
	./procx

clean:
	rm -f procx