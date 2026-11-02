<p align="center">
  <img src="docs/images/sparrow-logo.png" width="200" alt="Sparrow 的 logo：一隻飛行中的像素風麻雀">
</p>

<h1 align="center">SparrowFlight</h1>

<p align="center">打造你的小型四軸無人機</p>

<p align="center"><a href="README.md">English</a> | 繁體中文</p>

SparrowFlight 是以容易取得的零件為設計前提的小型四軸無人機平台。它的無人機 Sparrow 由現成模組（Seeed XIAO ESP32-S3、GY-521 IMU、GY-530 ToF 測距模組）、可由一般 PCB 廠製作與組裝的 2 層飛控板、從單片 100 × 100 mm 碳纖維板切割的機架，以及常見的 720 空心杯馬達組成，適合想要親手組裝、理解並修改小型無人機的創客、學生與教育工作者。

機架、飛控板與韌體正在整理，將以開放授權釋出，詳見[專案進度](#專案進度)。Sparrow 以 iOS App「SparrowFlight」操控。

<p align="center">
  <img src="docs/images/sparrow-front.jpg" width="380" alt="Sparrow 正面：碳纖維機架、四顆空心杯馬達和 55 mm 槳，1S 電池綁在機架上方">
  <img src="docs/images/sparrow-back.jpg" width="380" alt="Sparrow 底部：XIAO ESP32-S3 飛控板和朝下的 ToF 感測器">
</p>

## 特色

- **現成零件**：主控與感測器都是一般電子零件通路販售的模組，馬達、螺旋槳與電池則是小型無人機的標準零件。
- **為量產設計**：飛控板附 JLCPCB 組裝用的 BOM 與座標檔，機架為單一 DXF 檔，可直接 CNC 切割。
- **定高**：朝下的 ToF 測距維持離地高度，並用於按鈕起飛與自動降落。
- **手機 WiFi 操控**：無人機自建 WiFi 熱點並以藍牙廣播供 App 搜尋，也相容 ESP-Drone 的 commander 封包。
- **機上飛行紀錄**：黑盒子將最近幾次飛行記錄在 flash，供事後分析。

## 展示

<a href="docs/videos/test-flight.mp4"><img src="docs/images/test-flight.gif" width="240" alt="Sparrow 從頂樓起飛、懸停，用 iPhone 遙控"></a>

完整影片（36 秒）：[docs/videos/test-flight.mp4](docs/videos/test-flight.mp4)。

## 專案進度

| 元件 | 版本 | 狀態 |
|---|---|---|
| 機架 | 1.0 | 已完成飛行測試，設計檔整理中，將放在 [hardware/](hardware/README.md) |
| 飛控板 | 1.0 | 已完成飛行測試，設計檔整理中，將放在 [hardware/](hardware/README.md) |
| 韌體 | 1.0.0 | 已完成飛行測試，原始碼整理中，將放在 [firmware/](firmware/README.md) |
| SparrowFlight iOS App | 1.0.0 | 等待 App Store 上架，見 [software/](software/README.md)（英文） |

機架、飛控板與韌體將連同材料清單與組裝說明公開於本 repo，授權見 [LICENSE.md](LICENSE.md)（英文）。

## 規格

| | |
|---|---|
| 機架 | 100 × 100 mm，1.5 mm 碳纖維，卡扣式起落架 |
| 飛控板 | 40 × 40 mm、2 層板，以矽膠防震球懸吊於機架下方 |
| 主控 | Seeed XIAO ESP32-S3 Plus，或可傳輸相機畫面的 XIAO ESP32-S3 Sense |
| 感測器 | MPU-6050 IMU（GY-521 模組）、朝下的 VL53L0X ToF 測距（GY-530 模組） |
| 電源 | 1S 鋰聚電池（PH2.0 接頭）、TPS63020 升降壓轉換器、滑動電源開關 |
| 馬達 | 4 顆 720 空心杯有刷馬達、55 mm 槳、低側 MOSFET 驅動 |
| 連線 | 無人機端 WiFi 熱點，藍牙廣播供搜尋 |
| 飛行時間 | 400 mAh 20C 電池約 3.5–4 分鐘 |

## 零件

Sparrow 使用一般電子零件與模型零件通路都買得到的零件；飛控板與機架依設計檔製作，見[專案進度](#專案進度)。

| 零件 | 數量 | 用途 |
|---|---|---|
| [Seeed XIAO ESP32-S3 Plus](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)（或 XIAO ESP32-S3 Sense），附 WiFi 天線 | 1 | 飛行電腦：讀取感測器、執行控制迴圈、驅動馬達，並提供 WiFi 熱點與藍牙；Sense 版另有朝下的相機 |
| GY-521 模組（[MPU-6050](https://invensense.tdk.com/products/motion-tracking/6-axis/mpu-6050/)） | 1 | 六軸 IMU（陀螺儀與加速度計），用於姿態估測 |
| GY-530 模組（[VL53L0X](https://www.st.com/en/imaging-and-photonics-solutions/vl53l0x.html)） | 1 | 朝下的 ToF 測距，用於定高 |
| 720 空心杯有刷馬達，正轉、反轉各 2 顆 | 4 | 動力 |
| 55 mm 螺旋槳，適用 1 mm 馬達軸 | 2 對 | 正槳、反槳各 2 片，建議多備幾片 |
| 1S 鋰聚電池，400 mAh，PH2.0 接頭 | 1 | 電源，約可飛行 3.5–4 分鐘 |
| 馬達橡膠套、M2 螺絲、尼龍防鬆螺帽、尼龍墊片與矽膠防震球 | 各 4 | 固定馬達與飛控板 |
| 飛控板，40 × 40 mm | 1 | 向 PCB 廠下單，由廠商焊好電源與馬達驅動電路 |
| 碳纖維機架，1.5 mm，100 × 100 mm | 1 | 依單一 DXF 檔 CNC 切割 |

完整材料清單將隨硬體設計檔一起公開。

## APP

<img src="docs/images/app-flying.jpg" width="720" alt="SparrowFlight App 飛行中的畫面：左邊是升降和轉向搖桿，右邊是左右和前後搖桿，中間是高度 62 cm，下方是降落和緊停按鈕">

iOS 版 SparrowFlight 透過藍牙搜尋附近的無人機、加入所選無人機的 WiFi 網路，並在開放操控前檢查無人機狀態。支援按鈕起飛與降落，或以推力搖桿控制高度；可在搖桿後方顯示機上相機畫面，並編輯飛行參數。主控台與每次連線的飛行紀錄可協助調校。App 不收集任何個人資料。

文件：[App 說明](software/README.md)（英文）與[隱私權政策](software/PRIVACY.md)（英文）。

## 支援與貢獻

問題與回報請見 [SUPPORT.md](SUPPORT.md)（英文）。參與方式請見 [CONTRIBUTING.md](CONTRIBUTING.md)（英文），所有參與者都應遵守[行為準則](CODE_OF_CONDUCT.md)（英文）。

## 授權

文件與照片採用 CC BY-SA 4.0。機架（CC BY-NC-SA 4.0，改自 [LiteWing](https://github.com/Circuit-Digest/LiteWing)）、飛控板（CERN-OHL-S-2.0）與韌體（GPL-3.0）將以各自的授權釋出。iOS App 不開源。詳見 [LICENSE.md](LICENSE.md)（英文）。

「SparrowFlight」、「Sparrow」名稱與麻雀 logo 不在上述授權範圍內。設計公開後，可依授權製作並分享衍生版本，但不得宣稱為官方版本。
