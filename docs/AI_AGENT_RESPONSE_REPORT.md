# Phan hoi AI agent - C4 saved TTS voice profile

Ngay: 2026-08-02

## Ket qua moi nhat

Da sua dung condition C4 trong `AI_AGENT_REQUEST.md` va da push source/test len `main` tai commit `1e05fb2`.

- Dubbing Local khong con goi `cloneVoice()` cho tung segment khi chon saved voice.
- Saved voice Local chi duoc cho phep voi Qwen3-TTS, vi backend nay co primitive session-level `crispasr_session_set_voice()`. Profile duoc dat mot lan cho moi signature preset/reference va duoc reuse cho cac segment sau trong cung session.
- Runtime Local khac co clone-per-request (vi du OmniVoice) bi chan truoc khi Run TTS, voi loi ro rang va khong fallback. API Gateway saved voice cung bi chan ro rang; Direct Colab giu cache profile theo worker session nhu truoc.
- Project chi luu durable `ttsVoiceId`; reference path/text la internal execution data, khong thanh control UI, khong duoc persist vao project va duoc mask trong log.

## Bang chung tu dong

- Targeted: `TestDubbingProject`, `TestTtsRequestValidator`, `TestColabTtsRunner`, `TestColabVoiceCloneRunner`, `QmlRouteSmoke`: **6/6 PASS**.
- Full gate sau khi sua: **39/39 CTest PASS** (60.51 giay).
- Regression moi kiem tra ca hai diem: saved voice Qwen di qua `synthesize` thay vi `voice-cloning`; runtime Local khong co persistent profile bi chan va khong clone lai.
- Graphify da duoc cap nhat sau source edit. Khong mo EXE/browser va khong dieu khien GUI cua nguoi dung.

## Files chinh

- `src/controllers/dubbing/DubbingSynthesisJob.cpp`
- `src/controllers/dubbing/DubbingController.cpp`
- `src/tts/TtsSavedVoiceProfile.h`
- `src/tts/TtsRequestValidator.cpp`, `src/tts/TtsEngineInstance.cpp`
- `src/tts/backends/Qwen3Backend.cpp`, `src/tts/backends/Qwen3Backend.h`
- `tests/test_DubbingProject.cpp`, `tests/test_TtsRequestValidator.cpp`

## Package

`out/LA-Studio-0.0.2.17/LA-Studio-0.0.2.17.exe` (SHA-256 `665B4FD4AD9C76774639466907AE46B553BCC713BCB3A309D97341E1EE2862C3`) la artifact lich su tao **truoc** sua C4 nay. No khong duoc ghi nhan la package cua source `1e05fb2`, va da khong bi ghi de de ton trong quy tac khong overwrite candidate cu.

Vi request yeu cau dung ten `0.0.2.17` nhung artifact ten do da ton tai, chua co package moi nao duoc tao. Can mot version/ten candidate ke tiep duoc cho phep truoc khi dong goi source nay.

## Manual acceptance con mo

Automated tests khong thay the live acceptance: can chay Qwen3-TTS that voi saved preset de nghe audio va xac nhan native runtime set/reuse profile, va can test worker Direct Colab that. Khong co bang chung GUI/live service nao bi khai bao la PASS.
