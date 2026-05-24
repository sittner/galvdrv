.PHONY: svf upload clean

svf:
	vivado -mode batch -source build.tcl

upload: svf
	curl -X POST http://192.168.4.1/api/bitstream --data-binary @build/blink.svf

clean:
	rm -rf build
