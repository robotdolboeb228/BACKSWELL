# Backswell

Reverse-reverb riser plugin (AU + VST3 + Standalone), built with JUCE.

## Как пользоваться

1. Нажми **CAPTURE** и проиграй в DAW вокал или инструмент через плагин, затем нажми CAPTURE ещё раз.
   Либо перетащи аудиофайл прямо в окно плагина.
2. Плагин сам делает реверс-ревер. Ручка **RISE** задаёт длину нарастания.
3. **LISTEN / LOOP** — прослушать результат.
4. Перетащи волну в таймлайн DAW (или нажми **EXPORT**, чтобы сохранить WAV).

Прочее: **DE-ESS** убирает резкие «С» из обратного реверба, **INSTRUMENT MODE** отключает это,
**APPEND DRY CLIP** добавляет оригинал после райзера, эффекты Phone / Chorus / Drive / Tremolo,
фабричные и пользовательские пресеты (SAVE).

## Сборка через GitHub

1. Создай репозиторий на GitHub и залей туда содержимое этой папки.
2. Вкладка **Actions** → **Build macOS** (запускается при пуше или вручную через *Run workflow*).
3. Когда сборка закончится, скачай артефакт **Backswell-macOS**: внутри `Backswell.vst3.zip`,
   `Backswell.component.zip` и `Backswell.app.zip` (universal: Apple Silicon + Intel, macOS 11+).

## Установка

```
unzip Backswell.vst3.zip       -d ~/Library/Audio/Plug-Ins/VST3/
unzip Backswell.component.zip  -d ~/Library/Audio/Plug-Ins/Components/
xattr -cr ~/Library/Audio/Plug-Ins/VST3/Backswell.vst3
xattr -cr ~/Library/Audio/Plug-Ins/Components/Backswell.component
```

Плагин подписан ad-hoc (без Apple Developer ID), поэтому `xattr -cr` нужен, чтобы снять карантин.
После установки перезапусти DAW (для Logic — пересканируй плагины).

## Заметки

- Захваченный клип не сохраняется в проекте DAW (сохраняются только параметры).
- JUCE распространяется под AGPLv3 / коммерческой лицензией — учитывай это при распространении плагина.
