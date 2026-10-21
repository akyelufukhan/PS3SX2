# PS3SX2

[English](README.md) | **Türkçe**

**PS3 Slim ve Super Slim** için kapak akışlı (cover flow) bir PS2 oyun başlatıcısı.

Bu modellerde PS2 çipleri olmadığı için PS2 diskleri çalışmaz. Ama sistem yazılımında
Sony'nin PS2 yazılım emülatörü `ps2_netemu` hâlâ duruyor; PlayStation Store'daki PS2
Classics oyunları onunla çalışıyor. PS3SX2, `PS2ISO` klasöründeki oyunları bir kapak
rafı olarak listeler. Birini seçtiğinde oyunu bu emülatöre teslim eder.

İsim "PS3 Slim × PS2" diye okunur: S Slim'den, X oyunu başlatan ✕ tuşundan, 2 de
PS2'den geliyor.

> **Durum: erken geliştirme aşamasında.** Arayüz ve PS2 Classics ile başlatma yolu
> yazıldı ama henüz bir konsolda çalıştırılmadı. Pürüzler olabilir, bulduklarını
> bildirirsen sevinirim.

## Özellikler

- Dahili diskteki ve tüm USB portlarındaki (`/dev_usb000` - `/dev_usb007`) `PS2ISO` klasörünü tarar
- Her oyunun seri kodunu (`SLUS-20946`) doğrudan disk imajındaki `SYSTEM.CNF`'den okur,
  imaj şifreliyse dosya adından alır
- 3D kapak rafı: seçili kapak sana bakar, diğerleri iki yanda açılı durur
- Seçili kapağın altında oyunun adı, seri kodu, bölgesi, boyutu ve formatı
- PNG veya JPG kapaklar, boyut sınırı yok
- Türkçe ve İngilizce arayüz, konsolun sistem diline göre otomatik seçilir
- `.BIN.ENC` oyunları PS2 Classics Placeholder'a yükler (HEN ve CFW'de), sonra
  placeholder'ı XMB'den başlatırsın

## Uyumluluk

| Konsol | Yazılım | `.BIN.ENC` (PS2 Classic) | `.iso` |
|---|---|---|---|
| Super Slim (CECH-4xxx) | HEN | Yazıldı, test edilmedi | HEN'de mümkün değil |
| Slim | HEN | Yazıldı, test edilmedi | HEN'de mümkün değil |
| Slim, CFW kurulabilen | Cobra'lı CFW | Yazıldı, test edilmedi | Planlandı |

HEN, PS2 ISO bağlayamıyor. Bu yüzden HEN'de her oyunun `.BIN.ENC` olması gerekiyor.
İki başlatma yolunun ayrıntıları [docs/launching.md](docs/launching.md) dosyasında
(İngilizce).

## Konsolda gerekenler

1. **HEN veya CFW**
2. `.BIN.ENC` oyunlar için yüklü bir **PS2 Classics Placeholder** (`/dev_hdd0/game/PS2U10000`)
3. `PS2ISO` klasöründe oyunların: `.BIN.ENC` (sahip olduğun disklerden, PS2 Classics GUI
   gibi bir PC aracıyla hazırlanmış) ya da `.iso`

```
/dev_hdd0/PS2ISO/                       (USB bellekte /dev_usb000/PS2ISO/)
├── God of War II.iso
├── Burnout 3 [SLUS-21050].BIN.ENC
├── Burnout 3 [SLUS-21050].BIN.ENC.CONFIG    isteğe bağlı ps2_netemu ayarları
└── covers/
    ├── SCUS-97481.png
    └── SLUS-21050.jpg
```

### Kapaklar

PS3SX2 kapağı şu sırayla arar:

1. `PS2ISO/covers/<SERİ KOD>.png` veya `.jpg`, örneğin `covers/SLUS-21050.jpg`
2. Oyunun yanında, oyunla aynı adı taşıyan resim: `Burnout 3 [SLUS-21050].png`

Kapağı olmayan oyun düz renkli bir kutuyla gösterilir. Resimler yüklenirken
256×360'a küçültülür.

### Dosya adları

Görünen başlık dosya adından üretilir. Seri kod etiketleri silinir, `_` boşluğa
dönüşür. Aşağıdakilerin hepsi "Burnout 3" olarak görünür:

```
Burnout 3.iso
Burnout 3 [SLUS-21050].BIN.ENC
SLUS_210.50.Burnout_3.iso        Open PS2 Loader adlandırması
```

`.BIN.ENC` dosyalarında seri kod yalnızca dosya adından okunabilir. Kapakların seri
koda göre bulunmasını istiyorsan kodu dosya adında bırak.

## Kontroller

| Tuş | İşlev |
|---|---|
| Sol / Sağ | Önceki / sonraki oyun (basılı tutunca kayar) |
| L1 / R1 | 5 oyun atla |
| ✕ | Seçili oyunu başlat |
| △ | Klasörleri yeniden tara |
| ○ | Çıkış ya da kopyalamayı iptal |

## Derleme

Birim testleri C derleyicisi olan her makinede çalışır, PS3 araç zinciri gerekmez:

```bash
make test
```

PS3 paketi için [PSL1GHT](https://github.com/ps3dev/PSL1GHT) (açık kaynak PS3 SDK'sı),
FreeType için [ps3libraries](https://github.com/bucanero/ps3libraries) ve libfont ile
birlikte [Tiny3D](https://github.com/wargio/tiny3D) gerekir:

```bash
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV
make pkg
```

Sonuç `ps3sx2.pkg` dosyasıdır. GitHub'a yapılan her push'ta da derlenir: **Actions**
sekmesinden bir çalıştırmayı açıp `ps3sx2-…` çıktısını indirebilirsin.

### Kaynak düzeni

| Yol | İçinde ne var |
|---|---|
| `source/main.c` | Ana döngü, kol girişi, başlatma akışı |
| `source/gfx.c` | Tiny3D çizimi: kapak rafı, yazı, kapak dokuları |
| `source/library.c` | Klasör tarama, dosya adından başlık |
| `source/iso9660.c` | Normal ve ham disk imajlarından `SYSTEM.CNF` okuma |
| `source/serial.c` | `SYSTEM.CNF` ve dosya adında seri kod bulma, bölge tespiti |
| `source/launcher.c` | PS2 Classics kopyalama, Cobra ISO taslağı |
| `source/coverflow.c` | Kapak rafı yerleşimi ve animasyon hesapları |
| `source/covers.c` | Her oyunun kapağının aranacağı yerler |
| `source/lang.c`, `source/text.c` | Türkçe ve İngilizce metinler, UTF-8'den font kodlamasına çeviri |
| `tests/` | `main.c` ve `gfx.c` dışındaki her şey için birim testleri |

PS3 API'lerine yalnızca `main.c` ve `gfx.c` dokunur. Yeni mantığı ayrı bir modüle koy
ki `make test` ile test edilebilsin.

## Yol haritası

- [ ] Gerçek konsolda ve RPCS3'te çalıştırıp kapak rafını ayarlamak
- [ ] CFW'de düz `.iso` oyunları Cobra ile başlatmak
- [ ] XMB'ye dönmek yerine placeholder'ı doğrudan başlatmak
- [ ] FAT32 USB'lerdeki parçalı `.iso.0` / `.iso.1` imajları
- [ ] Hazır `ps2_netemu` CONFIG veritabanı
- [ ] Kapak yansımaları, analog çubuk, ayarlar ekranı
- [ ] Daha fazla dil

## Katkı

Issue ve pull request'ler açıktır. Pull request açmadan önce:

- `make test` çalıştır ve geçtiğinden emin ol
- PS3'ten bağımsız mantığı testleriyle birlikte kendi modülüne koy
- yeni bir dil eklemek için onu `lang_id`'ye ekle ve `source/lang.c`'de sütununu doldur.
  Testler her metnin fontla çizilebildiğini kontrol eder.

## Yasal

PS3SX2'nin Sony Interactive Entertainment ile bir bağı yoktur, onlar tarafından
desteklenmez. "PlayStation", "PS2" ve "PS3" sahiplerinin ticari markalarıdır.

Bu repoda oyun, sistem yazılımı, BIOS, emülatör dosyası ya da kapak resmi yoktur ve
eklenmemelidir. Sahip olduğun disklerdeki oyunları kullan.

## Lisans

[GNU GPL v3](LICENSE).

## Teşekkürler

- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) ve ps3dev topluluğu
- Çizim ve fontlar için Hermes'in [Tiny3D](https://github.com/wargio/tiny3D)'si
- PS2 oyunlarının nasıl bağlandığını kaynak koduyla belgeleyen aldostools'un
  [webMAN MOD](https://github.com/aldostools/webMAN-MOD)'u
- Bu projenin CI kurulumuna temel olan bucanero'nun
  [Apollo Save Tool](https://github.com/bucanero/apollo-ps3)'u
