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
        'Very steep downhill. Brake as needed', 'Slope reading unavailable', 'Maintain speed')
    for ($index = 0; $index -lt $phrases.Length; $index++) {
        $outputPath = Join-Path $assetDirectory ('prompt-{0}.wav' -f ($index + 1))
        $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
        $speech.SetOutputToWaveFile($outputPath, $format)
        $speech.Speak($phrases[$index])
        $speech.SetOutputToNull()
        Get-Item -LiteralPath $outputPath | Select-Object Name, Length
    }
} finally { $speech.Dispose() }
