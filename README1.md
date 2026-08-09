# LA Studio — README trung thực (bản nội bộ)

> Tài liệu này mô tả trạng thái đã có bằng chứng của nhánh `main` tại phiên
> bản `0.0.2.32`. Nó không phải tuyên bố rằng mọi model, notebook Colab hay
> dịch vụ bên ngoài đã được nghiệm thu trực tiếp.

LA Studio là ứng dụng desktop C++/Qt cho các luồng công việc âm thanh và
video: nhận dạng tiếng nói, dịch, tổng hợp tiếng nói, nhân bản/thiết kế giọng,
tách giọng, căn chỉnh phụ đề và dubbing. Đây là một fork đang được phát triển
để sử dụng nội bộ, chưa phải bản phát hành công khai được hỗ trợ.

## Trạng thái hiện tại

- Nền tảng được build và đóng gói: **Windows x64, MSVC 2022, Qt 6.9.x**.
  MinGW chỉ mang tính thử nghiệm; không có gói Linux hoặc macOS đã được xác
  nhận.
- Bản portable nội bộ hiện có là
  `out/LA-Studio-0.0.2.32/LA-Studio-0.0.2.32.exe`.
- Gói này dùng cho kiểm thử nội bộ. Thành phần eSpeak NG đã được kiểm tra
  SHA-256 nhưng không có chữ ký Authenticode; vì vậy gói không được quảng bá
  hoặc phân phối như bản public release.
- Lần kiểm thử tự động gần nhất của phạm vi nguồn hiện tại đạt **39/39 CTest
  pass**. Đây là bằng chứng cho regression/unit/offscreen tests, **không phải**
  bằng chứng rằng toàn bộ GUI trên máy người dùng, API Gateway hoặc Colab đã
  chạy thành công với dữ liệu thật.
- Không có lời cam kết về tốc độ, chất lượng model, khả năng sẵn có của Colab,
  hay tính tương thích của mọi GPU/driver. Các dịch vụ và notebook bên ngoài
  vẫn cần được kiểm thử thủ công cho từng phiên làm việc.

## Cách thực thi AI

LA Studio có ba con đường riêng biệt. Chúng không tự thay thế lẫn nhau:

| Route | Mục đích | Trạng thái/giới hạn |
| --- | --- | --- |
| Local CPU | Chạy model/runtime đã được người dùng cài rõ ràng trên máy. | Không tự tải model hay tự bật local fallback khi đã chọn remote. Local GPU offload không phải route được hỗ trợ. |
| API Gateway | Gọi Gateway bằng URL và API key của người dùng. | Chỉ hoạt động khi Gateway cung cấp endpoint/model tương ứng. Gateway không chuyển tiếp sang Colab. |
| Direct Colab GPU | Gọi đúng notebook worker bằng URL HTTPS và bearer token tạm thời. | Mỗi capability có session riêng, URL/token chỉ ở bộ nhớ phiên và phải kết nối lại sau khi Colab/tunnel reset. Colab không chuyển tiếp sang Gateway hay Local. |

Ứng dụng chỉ bật một worker Direct Colab sau khi kiểm tra `/health` và
`/v1/capabilities` xác nhận worker sẵn sàng, dùng CUDA và phục vụ đúng
capability/model đã chọn. Một URL/token hợp lệ ở studio này không mặc định
có hiệu lực ở studio khác.

## Các khu vực chức năng có trong mã nguồn

Các màn hình và controller hiện có bao gồm:

- Speech-to-Text, Text-to-Speech, Voice Cloning và Voice Design.
- Voice Isolator (vocals/background), Forced Alignment và Subtitle OCR.
- Translation Studio, LLM Chat và Dubbing Studio.
- Model Gallery, thiết lập route, lịch sử, preview/playback, log và export.

Danh sách trên có nghĩa là dự án có UI và luồng mã tương ứng; nó **không** có
nghĩa là mọi model trong gallery đều đã được tải, mọi runtime local đều có sẵn,
hay mọi provider remote đang trực tuyến. Hãy kiểm tra model, route và worker
ngay trong từng studio trước khi chạy tác vụ.

### Dubbing theo hàng đợi nhiều media

Trong Download page và Dubbing source panel, có thể dán nhiều **public URL**,
mỗi dòng một URL. Các URL được tải tuần tự và các media tải xong trở thành các
mục có thể chọn trong hàng đợi. Việc này không cam kết hỗ trợ scraping trang
có đăng nhập, DRM hoặc link hết hạn.

Với các mục đã chọn, Dubbing batch có thể chạy một hay nhiều tác vụ:

- tách audio, cho `vocals.wav` và `background.wav`;
- STT, cho `source.srt`;
- dịch, cho `translated.srt` (cần STT trước);
- TTS/giọng clone đã lưu và được đồng ý, cho `voice.wav` (cần STT và dịch
  trước).

Mỗi media chạy tuần tự qua `DubbingJobRunner` thật với bản sao project riêng.
Khi một mục lỗi, lỗi được gắn vào đúng mục và hàng đợi tiếp tục với mục sau;
không có local/remote fallback âm thầm. Artifacts của một batch nằm dưới:

```text
C:\Users\<user>\.lastudio\dubbing\batch-output\<queue-item-id>\
```

Mỗi thư mục thành công có `project.ladub.json` cùng các output thực sự tạo ra.
Không có file output được tạo giả. URL đã nộp được xoá khỏi bộ nhớ của hàng đợi
sau khi thành công, lỗi hoặc huỷ và không được ghi vào project/history/settings
hay output manifest.

Batch dùng giọng TTS hiện được cấu hình, kể cả profile clone đã lưu và có xác
nhận đồng ý. Batch không tự tạo một danh tính giọng clone mới cho từng video:
việc tạo clone cần tên và consent rõ ràng trong Voice Cloning Studio.

## Những gì đã được kiểm tra

Tại source commit `f0ca7f4`, phạm vi batch Dubbing đã được kiểm tra bằng:

- test loopback với hai public-download giả lập thật, xác nhận tải tuần tự,
  staging file và xóa URL;
- test runner thật với hai media có dependency STT cố ý không thể khởi động,
  xác nhận từng mục kết thúc lỗi và hàng đợi không kẹt ở trạng thái running;
- build lại controller/QML/tests với MSVC; QML được đưa qua Qt AOT path;
- full CTest: **39/39 pass** trong 57.71 giây;
- kiểm tra gói `0.0.2.32`: FileVersion/ProductVersion khớp, hash SHA-256 là
  `CBABA45A673D4B8FE4AFE38FCE30946E63159C78440E5483BC7F482EE60F8F7F`, và
  19/19 runtime/license artifacts được tìm thấy. FFmpeg, FFprobe và Tesseract
  5.5.1 đã qua CLI health checks.

Chưa được tuyên bố là đã kiểm tra trong đợt này:

- chạy GUI thấy được trên máy người dùng;
- inference thật qua Gateway, tunnel Cloudflare hoặc Colab có credential thật;
- chất lượng/độ chính xác của transcript, dịch, giọng hay stem;
- toàn bộ model/runtime/driver có trong catalog.

Muốn nghiệm thu một route remote, hãy chạy notebook đúng model, lấy URL/token
mới, dùng **Check connection** ở studio tương ứng, rồi chạy một media mẫu và
xác nhận output trước khi đưa vào công việc thật.

## Quyền riêng tư và dữ liệu

- Input remote chỉ được gửi tới route người dùng chọn cho tính năng đó.
- API Gateway key nằm trong secure Settings store; URL/token Colab là thông
  tin phiên tạm thời trong bộ nhớ.
- Ứng dụng không được thiết kế để chép credentials hay tự chuyển request giữa
  Gateway, Colab và Local.
- Tuy vậy, khi chọn API Gateway hoặc Direct Colab, media/text sẽ rời máy để
  đến endpoint do người dùng cấu hình. Người dùng chịu trách nhiệm về quyền
  sử dụng dữ liệu, điều khoản provider và consent của voice reference.

## Build từ source

Đây là source của fork nội bộ; clone repo này thay vì giả định repository
upstream có đúng các thay đổi nội bộ:

```powershell
git clone https://github.com/khoinguyen59/kova-video-studio.git
cd kova-video-studio
.\scripts\bootstrap.bat -QtRoot C:\Qt\6.9.3
```

Cần Visual Studio 2022/Build Tools (MSVC x64), Qt 6.9.x `msvc2022_64`, CMake
3.21+, Ninja và Git. FFmpeg/FFprobe cần có trên `PATH` cho developer build;
gói portable stage runtime riêng. Xem chi tiết tại [docs/BUILD.md](docs/BUILD.md).

Để chạy regression suite:

```powershell
.\scripts\run_tests.ps1
```

Build/test thành công không cấp quyền dùng hoặc phân phối model của bên thứ ba.
Hãy đọc license, model card và điều khoản của từng runtime/model trước khi dùng.

## Giấy phép và nguồn gốc

Source này là fork của
[dduongtrandai/LA-Studio](https://github.com/dduongtrandai/LA-Studio) và được
cấu hình theo **AGPL-3.0-only**; xem [LICENSE](LICENSE). Các model, runtime,
notebook dependency và gói nhị phân có thể mang license/điều khoản riêng.
Không có bảo hành về khả năng hoạt động, chất lượng output hay sự sẵn có của
dịch vụ bên ngoài.

## Tài liệu hữu ích

- [Build trên Windows](docs/BUILD.md)
- [Remote workers, route isolation và preflight](docs/REMOTE_WORKERS.md)
- [Báo cáo batch Dubbing hiện tại](docs/AI_AGENT_RESPONSE_REPORT.md)
- [Tóm tắt trạng thái dự án](docs/AI_AGENT_REPORT_SUMMARY.md)
