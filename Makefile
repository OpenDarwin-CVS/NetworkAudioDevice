pbxbuild:
	pbxbuild

install:
	pbxbuild install
	rm -rf /System/Library/Extensions/NetworkAudioDriver.kext
	mv /tmp/NetworkAudioDriver.dst/System/Library/Extensions/NetworkAudioDriver.kext \
        /System/Library/Extensions/

load:
	 kextload -c -s . -v 6 /System/Library/Extensions/NetworkAudioDriver.kext \
        > kextload.log

unload:
	kextunload /System/Library/Extensions/NetworkAudioDriver.kext
	sleep 1
	kextunload /System/Library/Extensions/NetworkAudioDriver.kext

clean:
	rm -rf build /tmp/NetworkAudioDriver.dst /System/Library/Extensions/NetworkAudioDriver.kext
