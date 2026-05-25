.PHONY: bit upload clean

bit:
	vivado -mode batch -source build.tcl

upload: bit
	curl -X POST http://192.168.4.1/api/bitstream --data-binary @build/blink.bit

clean:
	rm -rf build
