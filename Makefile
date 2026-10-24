all: cat tee

tee: tee.c
cat: cat.c

clean:
	rm -f cat tee
