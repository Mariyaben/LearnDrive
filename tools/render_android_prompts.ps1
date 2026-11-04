param([int[]]$PromptIds = @(), [switch]$Fragments)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$assetDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'android/app/src/main/assets/audio'
New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null
$speech = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $speech.SelectVoice('Microsoft Zira Desktop')
    $speech.Rate = 0
    $phrases = @('Uphill. Lower gear if needed', 'Ease off accelerator', 'Brake gently', 'Collision risk. Brake now',
        'Camera is on. Watch the road and keep a safe gap', 'Obstacle in your path. Slow down and be ready to stop',
        'Road user ahead. Slow down and give them space', 'Traffic ahead. Keep a safe gap',
        'Uphill. Steady power', 'Downhill. Control speed',
        'Left bend ahead. Ease off early and stay in your lane', 'Right bend ahead. Ease off early and stay in your lane',
        'Downhill. Use engine braking',
        'Engine revs are high. Shift up one gear when safe', 'Engine revs are low under load. Shift down one gear when safe',
        'Obstacle ahead. Keep space and be ready to stop', 'Obstacle ahead. Keep space. Select a lower gear only if the engine struggles',
        'Road user ahead. Give them space', 'Collision risk nearby. Stay alert',
        'Left bend ahead. Stay in your lane and look through the bend', 'Right bend ahead. Stay in your lane and look through the bend',
        'Left bend ahead. Set your speed and gear before the bend. Stay in your lane', 'Right bend ahead. Set your speed and gear before the bend. Stay in your lane',
        'Obstacle ahead. Slow down smoothly. Change down only if the engine struggles as you slow',
        'Obstacle ahead. Keep a safe gap and be ready to stop', 'Obstacle ahead. Keep a safe gap. Change down only if the engine struggles',
        'Gentle uphill. Steady power', 'Gentle downhill. Watch speed', 'Steep uphill. Avoid engine strain',
        'Steep downhill. Suitable low gear', 'Steep downhill. Control speed', 'Very steep uphill. Controlled power',
        'Very steep downhill. Brake as needed', 'Slope reading unavailable', 'Maintain speed',
        'Hold lane and distance', 'Keep a safe gap', 'Slow down smoothly', 'Watch for pedestrians',
        'Prepare for the bend', 'Observe the road sign', 'Brake harder', 'Emergency stop',
        'Person ahead. Give space', 'Two-wheeler ahead. Give space', 'Vehicle ahead. Keep a gap',
        'Left bend ahead. Prepare early', 'Right bend ahead. Prepare early',
        'Junction ahead. Check for traffic', 'Left turn ahead. Check for traffic', 'Right turn ahead. Check for traffic',
        'Left turn in about ten metres', 'Right turn in about ten metres',
        'Left turn in about fifty metres', 'Right turn in about fifty metres',
        'Left turn ahead. Check your mirrors and signal left',
        'Right turn ahead. Check your mirrors and signal right',
        'Junction ahead. Check your mirrors and ease off the accelerator',
        'Set a suitable approach speed. Prepare to give way',
        'Ease off the accelerator and brake gently. Change down to second as you slow',
        'Change down to second gear and cover the brake',
        'Clutch down if you need to stop. Look both ways before you turn',
        'Straighten the wheel and add gentle accelerator',
        'Person ahead. Ease off the accelerator and cover the brake',
        'Two-wheeler ahead. Ease off the accelerator and cover the brake',
        'Vehicle ahead. Ease off and keep a safe gap',
        'Brake gently and be ready to stop',
        'Clutch down and hold the brake',
        'Change up to second gear', 'Change up to third gear', 'Change up to fourth gear', 'Change up to fifth gear',
        'Change down to second gear', 'Change down to third gear',
        'Clutch down as you stop',
        'When it is clear: first gear, check mirrors and signal, then release the clutch slowly',
        'Downhill. Change down to third and control your speed with the brake',
        'Steep downhill. Change down to second and control your speed with the brake',
        'Uphill. Change down to second and keep a steady accelerator',
        'Uphill. Change down to third and keep a steady accelerator',
        'Vehicle crossing ahead. Brake and wait',
        'Person crossing ahead. Stop and let them cross',
        'Oncoming vehicle. Keep left and ease off',
        'Person on the left. Ease off and give them space',
        'Person on the right. Ease off and give them space',
        'It has passed. Look both ways again before you move off',
        'You are drifting left. Move back to the centre of your lane',
        'You are drifting right. Keep left')
    for ($index = 0; $index -lt $phrases.Length; $index++) {
        if ($PromptIds.Count -gt 0 -and ($index + 1) -notin $PromptIds) { continue }
        $outputPath = Join-Path $assetDirectory ('prompt-{0}.wav' -f ($index + 1))
        $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
        $speech.SetOutputToWaveFile($outputPath, $format)
        $speech.Speak($phrases[$index])
        $speech.SetOutputToNull()
        if (($index + 1) -ge 44) {
            & python (Join-Path $PSScriptRoot 'trim_pcm_padding.py') $outputPath
            if ($LASTEXITCODE -ne 0) { throw 'Prompt padding trim failed.' }
        }
        Get-Item -LiteralPath $outputPath | Select-Object Name, Length
    }
    # Instructor phrase fragments composed per situation by the native planner
    # (ids and text must match Fragment/fragmentText in core/include/learndrive/types.hpp).
    $fragmentTexts = @(
        'Downhill.',
        'Steep downhill.',
        'Uphill.',
        'Bend ahead.',
        'Junction ahead.',
        'Left turn ahead.',
        'Right turn ahead.',
        'Oncoming truck.',
        'Oncoming bus.',
        'Oncoming vehicle.',
        'Oncoming two-wheeler.',
        'Vehicle crossing.',
        'Person crossing.',
        'Person on the left.',
        'Person on the right.',
        'Person ahead.',
        'Two-wheeler ahead.',
        'Vehicle ahead.',
        'Truck ahead.',
        'You are drifting left.',
        'You are drifting right.',
        'Brake gently.',
        'Brake now.',
        'Be ready to stop.',
        'Stop and wait.',
        'Ease off the accelerator.',
        'Cover the brake.',
        'Keep left.',
        'Give them space.',
        'Keep a safe gap.',
        'Change down to second.',
        'Change down to third.',
        'Clutch down.',
        'Hold the brake.',
        'Check your mirrors.',
        'Signal left.',
        'Signal right.',
        'Look both ways.',
        'Steady accelerator.',
        'Move back to the centre of your lane.',
        'Let them pass.',
        'Look again before you move off.')
    if ($Fragments -or $PromptIds.Count -eq 0) {
        for ($index = 0; $index -lt $fragmentTexts.Length; $index++) {
            $outputPath = Join-Path $assetDirectory ('fragment-{0}.wav' -f ($index + 1))
            $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
            $speech.SetOutputToWaveFile($outputPath, $format)
            $speech.Speak($fragmentTexts[$index])
            $speech.SetOutputToNull()
            & python (Join-Path $PSScriptRoot 'trim_pcm_padding.py') $outputPath
            if ($LASTEXITCODE -ne 0) { throw 'Fragment padding trim failed.' }
            Get-Item -LiteralPath $outputPath | Select-Object Name, Length
        }
    }
} finally { $speech.Dispose() }
