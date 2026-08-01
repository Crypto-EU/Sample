# HiveOS Custom Miner — BtcMole (AMD)

Wrapper-Paket, damit [BtcMole](https://github.com/keymole/btcmole) als HiveOS **Custom Miner** läuft.

## Binary einlegen

1. Von GitHub laden: `bmXXX.linux_amd64_amdgpu.zip`
2. Entpacken und ausführbar machen:

```bash
unzip bm079.linux_amd64_amdgpu.zip
chmod +x bm079
cp bm079 BitcoinPuzzle/hiveos-btcmole/btcmole
# oder: ln -sf bm079 BitcoinPuzzle/hiveos-btcmole/btcmole
```

## Tarball bauen

```bash
cd BitcoinPuzzle/hiveos-btcmole
tar -czvf btcmole-hive-1.0.0.tar.gz \
  h-manifest.conf h-config.sh h-run.sh h-stats.sh btcmole
```

URL des Tarballs in die Flight Sheet **Installation URL** eintragen.  
Miner name: `btcmole`

## Extra config

```text
bf -pz 71 +amdgpu
```

oder Pool:

```text
dig -s DEINE_SESSION +amdgpu
```

mit GFX-Override:

```text
HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu
```

Vollständige Anleitung: [`../ANLEITUNG-AMD-HIVEOS.md`](../ANLEITUNG-AMD-HIVEOS.md)
