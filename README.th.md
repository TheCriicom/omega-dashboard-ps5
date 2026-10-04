<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  แดชบอร์ดโอเพนซอร์สสำหรับเครื่อง PS5 ที่รัน homebrew ได้: เกม, Store, เพลง, เพื่อน และ party รวมอยู่ในที่เดียว<br>
  พัฒนาโดย <a href="https://outlinedigital.it"><b>TheCriicom</b></a>
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <b>ไทย</b> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">เว็บไซต์</a> ·
  <a href="https://play.omegasuite.it/installa">ติดตั้ง</a> ·
  <a href="server/README.md">โฮสต์เซิร์ฟเวอร์</a> ·
  <a href="client/README.md">การพัฒนา</a>
</p>

![หน้า Home ของ Omega](docs/screenshots/home.png)

Omega คือแดชบอร์ดที่สร้างมาสำหรับผู้ใช้ homebrew และเป็นหน้า Home ของคุณได้ถ้าต้องการ
(จะถามตอนเปิดใช้งานครั้งแรก): เกมที่ติดตั้งไว้และ homebrew อยู่ในแถวเดียวกัน
Store ติดตั้งได้ด้วยปุ่มเดียว เพลงเล่นต่อเนื่องระหว่างที่คุณเล่นเกม
และเพื่อนของคุณอยู่ห่างไปเพียงการกดปุ่มครั้งเดียว Omega ทำงานในรูปแบบ homebrew
(SDL2, เรนเดอร์ด้วยซอฟต์แวร์) และสื่อสารกับเซิร์ฟเวอร์ที่ใครก็โฮสต์ได้

## มีอะไรบ้าง

- **Home** ที่รวมเกมและ homebrew ไว้ด้วยกัน พื้นหลังแบบไดนามิกจากอาร์ตเวิร์ก
  และเปิดใช้งานได้โดยตรง (เกมผ่าน LncUtil, homebrew ผ่าน websrv, เพย์โหลด ELF
  ทำงานเบื้องหลัง)
- **Store** สำหรับ homebrew โอเพนซอร์ส: ค้นหา ชั้นวางตามหมวดหมู่ โหวต ให้คะแนน
  และแสดงความคิดเห็น ตรวจจับไฟล์ `.pkg`, `.zip` และ `.elf` ได้เอง
  และติดตั้งแต่ละไฟล์ไว้ในตำแหน่งที่ถูกต้อง
- **คลังเกมของฉัน**: ข้อมูลสำรองของเกมที่คุณเป็นเจ้าของ พร้อมลิงก์ดาวน์โหลดและปก
  เพิ่มได้จากเครื่องหรือจากโทรศัพท์ หรือนำเข้าจากไฟล์ JSON (นำเข้าครั้งเดียว
  หรือเชื่อมโยงไว้เพื่อซิงก์ต่อเนื่อง) เก็บไว้ในเครื่องเท่านั้น
- **เพลง** ฟังได้แม้ระหว่างเล่นเกม: วิทยุอินเทอร์เน็ต (radio-browser), Navidrome และ
  เซิร์ฟเวอร์ Subsonic อื่น ๆ, ไฟล์จาก USB, ลิงก์เสียงใดก็ได้ การเล่นทำงานใน
  เดมอนเบื้องหลัง (FFmpeg บิลด์เฉพาะเสียง) จึงไม่หยุดเมื่อคุณเปิดเกม
- **รีโมตบนโทรศัพท์**: เดมอนให้บริการหน้าเว็บที่พอร์ต 9095 สแกนโค้ด QR
  ที่แสดงบนเครื่อง ใส่ PIN แล้วควบคุมเพลง ส่งไฟล์เสียง และจัดการคลังเกมของฉัน
  ได้จากเบราว์เซอร์ของโทรศัพท์ แท็บเล็ต หรือพีซีเครื่องไหนก็ได้
- **ตรวจจับ HEN**: ตอนเริ่มต้น Omega จะรู้จัก OnionHEN, etaHEN, pldmgr หรือ
  ps5_autoloader บอกว่ายังขาดอะไร และเมื่อคุณยืนยันแล้ว จะติดตั้งและเปิดใช้งาน
  เซอร์วิสของมันในตำแหน่งที่ถูกต้อง บน OnionHEN จะเพิ่มหน้าหนึ่งในเมนูระหว่างเกม
  (L2 + R3) พร้อมปุ่มควบคุมเพลง
- **เครื่องมือระบบ**: อุณหภูมิ ค่าเกณฑ์ของพัดลม พื้นที่จัดเก็บ และตัวจัดการไฟล์
- **Community**: วอลล์ที่มีโพสต์ ไลก์ และความคิดเห็น แชตกลุ่ม คนที่คุณอาจรู้จัก
  เวลาเล่นเกม และกระดานจัดอันดับของเพื่อน
- **Party** พร้อมแชตและเสียง (Opus) คำเชิญเข้าเกม สถานะที่กำหนดเองได้
  (ออนไลน์, ไม่อยู่, ห้ามรบกวน, ซ่อนตัว)
- **Game Base**: เพื่อน คำขอเป็นเพื่อน และข้อความ
- **ความเป็นส่วนตัว**: การบล็อก การรายงาน กำหนดว่าใครส่งข้อความถึงคุณได้
  การส่งออกข้อมูล และการลบบัญชี
- **ธีม** เพลงประกอบบรรยากาศที่สร้างขึ้นอัตโนมัติ และเบราว์เซอร์สำหรับอ่าน
- **27 ภาษา**: แอปใช้ภาษาตามเครื่องโดยอัตโนมัติ (และเปลี่ยนได้ในการตั้งค่า)
- **เลือกเซิร์ฟเวอร์ได้เอง**: เพิ่มที่อยู่ของเซิร์ฟเวอร์ Omega ใดก็ได้ในการตั้งค่า
  ส่วนเซิร์ฟเวอร์ทางการยังคงใช้งานได้เสมอ

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![เวลาเล่นเกม](docs/screenshots/stats.png) |
| ![รีโมตบนโทรศัพท์ที่แสดงบนพีซี](docs/screenshots/remote-music.png) | ![คลังเกมของฉันบนพีซี](docs/screenshots/remote-library.png) |

## โครงสร้างของรีโพสิทอรี

| โฟลเดอร์ | |
|---|---|
| [`client/`](client) | แอปสำหรับเครื่องคอนโซล (C, SDL2) และบิลด์เดสก์ท็อปสำหรับพัฒนาบน Mac |
| [`daemon/`](daemon) | เพย์โหลดเบื้องหลัง: เครื่องเล่นเพลง หน้าเว็บรีโมตบนโทรศัพท์ คลังเกมของฉัน การแจ้งเตือนระหว่างเล่นเกม และ (เฉพาะเมื่อคุณเลือก) เปิด Omega อีกครั้งเมื่อกลับไปที่ Home |
| [`onionhen-plugin/`](onionhen-plugin) | ปลั๊กอิน OnionHEN: หน้า Omega ในเมนูระหว่างเกมพร้อมปุ่มควบคุมเพลง |
| [`server/`](server) | API Node.js, พร็อกซี, แผงควบคุมการดูแลเนื้อหา และ Docker Compose สำหรับโฮสต์เซิร์ฟเวอร์ |
| [`docs/`](docs) | สถาปัตยกรรมและรูปภาพ |

## การติดตั้ง Omega บนเครื่อง

คุณต้องมี PS5 ที่เจลเบรกแล้วและรองรับ homebrew: HEN อย่าง OnionHEN หรือ etaHEN
หรือลอนเชอร์อย่าง [websrv](https://github.com/ps5-payload-dev/websrv)
ร่วมกับตัวโหลดเพย์โหลด
วิธีที่ง่ายที่สุดคือเปิด [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
ในเบราว์เซอร์ของเครื่อง หรือจะดาวน์โหลดแพ็กเกจจากเว็บไซต์
แล้วคัดลอก `data/` ไปยังเครื่องผ่าน FTP ก็ได้

## การโฮสต์เซิร์ฟเวอร์

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

สคริปต์จะสร้างไฟล์ `.env` พร้อมค่าลับแบบสุ่มและเริ่มคอนเทนเนอร์
คู่มือฉบับเต็มซึ่งรวมการตั้งค่า HTTPS อัตโนมัติผ่าน Caddy อยู่ใน
[`server/README.md`](server/README.md) จากนั้นบนเครื่อง:
**การตั้งค่า → เซิร์ฟเวอร์ → เพิ่มเซิร์ฟเวอร์**

## การพัฒนา

- แอป: [`client/README.md`](client/README.md) — บิลด์สำหรับ PS5 ด้วย
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) และบิลด์เดสก์ท็อป
  ด้วย SDL2 จาก Homebrew ซึ่งสั่งงานผ่านไฟล์คำสั่งเพื่อทดสอบ UI ได้โดยไม่ต้องใช้เครื่อง
  คำแปลอยู่ใน `client/i18n/` (หนึ่งไฟล์ JSON ต่อหนึ่งภาษา)
- เซิร์ฟเวอร์: [`server/README.md`](server/README.md) — Node.js ≥ 20 และ PostgreSQL
  ทดสอบแบบ end-to-end ด้วย `npm test`
- สถาปัตยกรรม: [`docs/architecture.md`](docs/architecture.md)

ยินดีรับการมีส่วนร่วม: ดู [CONTRIBUTING.md](CONTRIBUTING.md)

## ผู้พัฒนา

Omega พัฒนาโดย **TheCriicom** — [outlinedigital.it](https://outlinedigital.it)

## สัญญาอนุญาต

Copyright © 2026 TheCriicom และผู้ร่วมพัฒนา Omega
Omega เป็นซอฟต์แวร์เสรี: [GNU GPL v3 หรือใหม่กว่า](LICENSE) ส่วนประกอบจากบุคคลที่สาม
และสัญญาอนุญาตของแต่ละส่วนระบุไว้ใน
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md)

Omega เป็นโครงการอิสระ ไม่มีส่วนเกี่ยวข้อง ไม่ได้รับการรับรอง
และไม่ได้รับการสนับสนุนจาก Sony Interactive Entertainment "PlayStation" และ "PS5"
เป็นเครื่องหมายการค้าของเจ้าของที่เกี่ยวข้อง Omega ไม่มีและไม่เผยแพร่เกมใด ๆ
