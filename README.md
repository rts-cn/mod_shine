# mod_shine

FreeSWITCH module for mp3 record.

This mod is based on https://github.com/toots/shine . Shine is a fast fixed-point mp3 encoding library. This mod was written many years ago when I learning to write FreeSWITCH modules. It probably doesn't build on the current version of FreeSWITCH.

The code is simple and hope still useful.

Pull Request is welcome.

## ToDo:

* [ ] Fix build on latest FreeSWITCH master
* [ ] Add cmake or autotools build scripts
* [ ] Add some test cases

## build

Put this mod in the freeswitch source dir, your freeswitch should already been built and installed.

```
cd freeswitch/src/mod/
mkdir rts
cd rts
git clone https://github.com/rts-cn/mod_shine
cd mod_shine
make
make install
```

## load

```
load mod_shine
```

## FAQ

Q: How this mod compares with mod_shout?

A: Please test and let us know by submit an issue.

Q: What License?

A: Same as FreeSWITCH. But Shine is GPL. So I'm not sure.

Q: Does it work in Windows?

A: I haven't use windows for many years, but there's some Windows project files, probably it still works.

Q: Do you accept Pull Request?

A: Sure. Thanks.
