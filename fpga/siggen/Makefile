.PHONY: bit upload clean

bit:
	vivado -mode batch -source build.tcl

upload: bit
	curl -X POST http://192.168.4.1/api/bitstream --data-binary @build/siggen.bit

clean:
	rm -rf build .Xil
	rm -f vivado.jou vivado.log vivado_*.backup.* clockInfo.txt
