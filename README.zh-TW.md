<p align="center">
  <img src="docs/images/sparrow-logo.png" width="200" alt="Sparrow 的 logo：一隻飛行中的像素風麻雀">
</p>

<h1 align="center">SparrowFlight</h1>

<p align="center">開源的小型四軸無人機，和遙控它的 iPhone App</p>

<p align="center"><a href="README.md">English</a> | 繁體中文</p>

SparrowFlight 是 **Sparrow** 背後的專案：一台 100 mm 的碳纖維四軸無人機，加上一片自己設計、以 Seeed XIAO ESP32-S3 為核心的 40 × 40 mm 飛控板。Sparrow 用 iPhone 上的 **SparrowFlight** App 透過 WiFi 遙控。

<p align="center">
  <img src="docs/images/sparrow-front.jpg" width="380" alt="Sparrow 正面：碳纖維機架、四顆空心杯馬達和 55 mm 槳，1S 電池綁在機架上方">
  <img src="docs/images/sparrow-back.jpg" width="380" alt="Sparrow 底部：XIAO ESP32-S3 飛控板和朝下的 ToF 感測器">
</p>

## 試飛

<a href="docs/videos/test-flight.mp4"><img src="docs/images/test-flight.gif" width="240" alt="Sparrow 從頂樓起飛、懸停，用 iPhone 遙控"></a>

2026 年 10 月在頂樓的試飛：在 App 裡選無人機、加入它的 WiFi、起飛和懸停。完整影片（36 秒）在 [docs/videos/test-flight.mp4](docs/videos/test-flight.mp4)。

## 進度

| 部分 | 版本 | 進度 |
|---|---|---|
| SparrowFlight iPhone App | 1.0.0 | 即將上架 App Store，見 [software/](software/README.md)（英文） |
| 機架 | v1.0 | 已經在飛，檔案整理中，會放在 [hardware/](hardware/README.md) |
| 飛控板 | v1.0 | 已經在飛，檔案整理中，會放在 [hardware/](hardware/README.md) |
| 韌體 | 1.0.0 | 已經在飛，原始碼整理中，會放在 [firmware/](firmware/README.md) |

機架、飛控板和韌體會公開在這個 repo，各自的授權見 [LICENSE.md](LICENSE.md)（英文）。

## 規格

| | |
|---|---|
| 機架 | 100 × 100 mm，1.5 mm 碳纖維，起落架直接卡上 |
| 飛控板 | 40 × 40 mm、2 層板，用防震球吊在機架下方 |
| 主控 | Seeed XIAO ESP32-S3 Plus，或可傳相機畫面的 XIAO ESP32-S3 Sense |
| 感測器 | MPU-6050 IMU（GY-521 模組）、朝下的 VL53L0X ToF 測距（GY-530 模組） |
| 電源 | 1S 鋰聚電池（PH2.0 接頭）、TPS63020 升降壓、滑動電源開關 |
| 馬達 | 4 顆 720 空心杯有刷馬達、55 mm 槳、低側 MOSFET 驅動 |
| 連線 | 無人機上的 WiFi 熱點，藍牙廣播讓 App 找到它 |
| 飛行時間 | 400 mAh 20C 電池約 1.5–2 分鐘 |

韌體功能：自穩模式、跟著地面高低變化的 ToF 定高、按鈕起飛和自動降落、速度模式、量一次就存起來的馬達 trim、機上黑盒子記錄、讓 App 找到附近無人機的藍牙廣播，以及用 XIAO ESP32-S3 Sense 時傳給 App 的相機畫面。

## App

<img src="docs/images/app-flying.jpg" width="720" alt="SparrowFlight App 飛行中的畫面：左邊是升降和轉向搖桿，右邊是左右和前後搖桿，中間是高度 62 cm，下方是降落和緊停按鈕">

SparrowFlight 用藍牙列出附近的無人機，點一下就加入它的 WiFi，連上後先檢查無人機，通過才出現搖桿。可以按按鈕起飛和降落，也可以用推力搖桿決定定高的高度；搖桿後方顯示機上相機的畫面，也能修改飛行參數。主控台和每次連線的飛行紀錄方便調校。App 不收集任何個人資料。

完整說明見 [software/README.md](software/README.md)（英文），以及[隱私權政策](software/PRIVACY.md)（英文）。

## Repo 內容

| 資料夾 | 內容 |
|---|---|
| [`software/`](software/) | SparrowFlight App 的說明和隱私權政策（App 本身不開源） |
| [`hardware/`](hardware/) | 機架和飛控板（尚未公開） |
| [`firmware/`](firmware/) | 飛控韌體（尚未公開） |
| [`docs/`](docs/) | 照片和試飛影片 |

## 支援

問題和回報：見 [SUPPORT.md](SUPPORT.md)（英文）。

## 安全

鋰聚電池受損、短路或過充可能起火：用合格的充電器充電，充電時不要離開。螺旋槳轉動時會割傷人：接著電池處理無人機時先拆槳，遠離人群飛行，並遵守當地的無人機法規。設計和軟體不提供任何保固。

## 名稱

「SparrowFlight」和「Sparrow」這兩個名稱，以及麻雀 logo，不在這個 repo 的授權範圍內。設計公開後，你可以依照授權製作、修改、分享 Sparrow，但請不要把你的版本說成官方版本。
