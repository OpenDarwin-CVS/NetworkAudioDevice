pbxbuild:
	pbxbuild

install:
	pbxbuild install

load:
	 kextload /tmp/NetworkAudioDriver.dst/System/Library/Extensions/NetworkAudioDriver.kext/

unload:
	kextunload /tmp/NetworkAudioDriver.dst/System/Library/Extensions/NetworkAudioDriver.kext/
	sleep 1
	kextunload /tmp/NetworkAudioDriver.dst/System/Library/Extensions/NetworkAudioDriver.kext/

clean:
	rm -rf build /tmp/NetworkAudioDriver.dst
