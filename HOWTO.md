###convert music from QOA to ADPCM

you already need to build and install KOS from the provided `wipeout_kos.tgz`

so you will have kos utils built

    install https://github.com/braheezy/goqoa

there are 11 tracks (`track01.wav` through `track11.wav`) in `wipeout/music` after you download the data file from the wipeout-rewrite blog

look near the end of this page: https://phoboslab.org/log/2023/08/rewriting-wipeout

    ./goqoa convert track01.qoa track01.wav
    $KOS_BASE/utils/wav2adpcm/wav2adpcm -n -i -t track01.wav track01.adpcm
    rm track01.qoa
    rm track01.adpcm

repeat through track11
