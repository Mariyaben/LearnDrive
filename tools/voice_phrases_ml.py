"""Malayalam words for every English phrase and sentence the coach can say (ld::fragmentText and
ld::promptText). Writes android/app/src/main/java/ai/learndrive/mobile/audio/VoicePhrases.kt.
Fails if a phrase the core can say has no Malayalam, or two of them would sound the same.
Usage: python tools/voice_phrases_ml.py"""
import re, sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
header = (root / 'core/include/learndrive/types.hpp').read_text(encoding='utf-8')
def texts(name):
    i = header.index('inline const char* %s(' % name); j = header.index('default:', i)
    return re.findall(r'return\s+"([^"]*)"', header[i:j])
fragments, prompts = texts('fragmentText'), texts('promptText')

ML = {
# ---- phrases (ld::Fragment, ids 1..77), joined by the core into one sentence per situation
'Downhill.': 'ഇറക്കമാണ്.',
'Steep downhill.': 'കുത്തനെയുള്ള ഇറക്കമാണ്.',
'Uphill.': 'കയറ്റമാണ്.',
'Bend ahead.': 'മുന്നിൽ വളവുണ്ട്.',
'Junction ahead.': 'മുന്നിൽ ജംഗ്ഷനുണ്ട്.',
'Left turn ahead.': 'മുന്നിൽ ഇടത്തേക്ക് തിരിയണം.',
'Right turn ahead.': 'മുന്നിൽ വലത്തേക്ക് തിരിയണം.',
'Oncoming truck.': 'എതിരെ ലോറി വരുന്നു.',
'Oncoming bus.': 'എതിരെ ബസ് വരുന്നു.',
'Oncoming vehicle.': 'എതിരെ വാഹനം വരുന്നു.',
'Oncoming two-wheeler.': 'എതിരെ ടൂവീലർ വരുന്നു.',
'Vehicle crossing.': 'വാഹനം കുറുകെ കടക്കുന്നു.',
'Person crossing.': 'ആൾ റോഡ് മുറിച്ചുകടക്കുന്നു.',
'Person on the left.': 'ഇടതുവശത്ത് ആളുണ്ട്.',
'Person on the right.': 'വലതുവശത്ത് ആളുണ്ട്.',
'Person ahead.': 'മുന്നിൽ ആളുണ്ട്.',
'Two-wheeler ahead.': 'മുന്നിൽ ടൂവീലറുണ്ട്.',
'Vehicle ahead.': 'മുന്നിൽ വാഹനമുണ്ട്.',
'Truck ahead.': 'മുന്നിൽ ലോറിയുണ്ട്.',
'You are drifting left.': 'വണ്ടി ഇടത്തോട്ട് നീങ്ങുന്നുണ്ട്.',
'You are drifting right.': 'വണ്ടി വലത്തോട്ട് നീങ്ങുന്നുണ്ട്.',
'Brake gently.': 'പതുക്കെ ബ്രേക്ക് ചവിട്ടുക.',
'Brake now.': 'ഉടനെ ബ്രേക്ക് ചവിട്ടുക.',
'Be ready to stop.': 'നിർത്താൻ തയ്യാറാവുക.',
'Stop and wait.': 'നിർത്തി കാത്തുനിൽക്കുക.',
'Ease off the accelerator.': 'ആക്സിലറേറ്റർ കുറയ്ക്കുക.',
'Cover the brake.': 'കാൽ ബ്രേക്കിന് മുകളിൽ വെക്കുക.',
'Keep left.': 'ഇടത്തോട്ട് ഒതുക്കുക.',
'Give them space.': 'അവർക്ക് ഇടം കൊടുക്കുക.',
'Keep a safe gap.': 'സുരക്ഷിതമായ അകലം പാലിക്കുക.',
'Change down to second.': 'ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റുക.',
'Change down to third.': 'ഗിയർ കുറച്ച് തേർഡിലേക്ക് മാറ്റുക.',
'Clutch down.': 'ക്ലച്ച് ചവിട്ടുക.',
'Hold the brake.': 'ബ്രേക്ക് ചവിട്ടിപ്പിടിക്കുക.',
'Check your mirrors.': 'മിററുകൾ നോക്കുക.',
'Signal left.': 'ഇടത്തേക്ക് ഇൻഡിക്കേറ്റർ ഇടുക.',
'Signal right.': 'വലത്തേക്ക് ഇൻഡിക്കേറ്റർ ഇടുക.',
'Look both ways.': 'ഇരുവശവും നോക്കുക.',
'Steady accelerator.': 'ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക.',
'Move back to the centre of your lane.': 'ലെയ്നിന്റെ നടുവിലേക്ക് തിരികെ വരിക.',
'Let them pass.': 'അവരെ കടന്നുപോകാൻ അനുവദിക്കുക.',
'Look again before you move off.': 'നീങ്ങുന്നതിന് മുമ്പ് ഒന്നുകൂടി നോക്കുക.',
'Sound the horn.': 'ഹോൺ അടിക്കുക.',
'Speed bump ahead.': 'മുന്നിൽ ഹമ്പുണ്ട്.',
'In 200 metres,': 'ഇരുനൂറ് മീറ്റർ കഴിഞ്ഞ്,',
'In 150 metres,': 'നൂറ്റമ്പത് മീറ്റർ കഴിഞ്ഞ്,',
'In 100 metres,': 'നൂറ് മീറ്റർ കഴിഞ്ഞ്,',
'In 50 metres,': 'അമ്പത് മീറ്റർ കഴിഞ്ഞ്,',
'turn left.': 'ഇടത്തേക്ക് തിരിയുക.',
'turn right.': 'വലത്തേക്ക് തിരിയുക.',
'keep left.': 'ഇടതുവശം ചേർന്ന് പോകുക.',
'keep right.': 'വലതുവശം ചേർന്ന് പോകുക.',
'make a U-turn.': 'യൂ-ടേൺ എടുക്കുക.',
'at the roundabout, take the first exit.': 'റൗണ്ട്എബൗട്ടിൽ ഒന്നാമത്തെ എക്സിറ്റ് എടുക്കുക.',
'at the roundabout, take the second exit.': 'റൗണ്ട്എബൗട്ടിൽ രണ്ടാമത്തെ എക്സിറ്റ് എടുക്കുക.',
'at the roundabout, take the third exit.': 'റൗണ്ട്എബൗട്ടിൽ മൂന്നാമത്തെ എക്സിറ്റ് എടുക്കുക.',
'at the roundabout, take the fourth exit.': 'റൗണ്ട്എബൗട്ടിൽ നാലാമത്തെ എക്സിറ്റ് എടുക്കുക.',
'enter the roundabout.': 'റൗണ്ട്എബൗട്ടിലേക്ക് കയറുക.',
'your destination is ahead.': 'ലക്ഷ്യസ്ഥാനത്ത് എത്തും.',
'Left turn here.': 'ഇവിടെ ഇടത്തേക്ക് തിരിയുക.',
'Right turn here.': 'ഇവിടെ വലത്തേക്ക് തിരിയുക.',
'Then turn left straight after.': 'അതിനുശേഷം ഉടനെ ഇടത്തേക്ക് തിരിയുക.',
'Then turn right straight after.': 'അതിനുശേഷം ഉടനെ വലത്തേക്ക് തിരിയുക.',
'Give way to traffic from the right.': 'വലതുവശത്തുനിന്ന് വരുന്ന വാഹനങ്ങൾക്ക് വഴി കൊടുക്കുക.',
'Pull over safely.': 'സുരക്ഷിതമായി ഒരു വശത്തേക്ക് ഒതുക്കി നിർത്തുക.',
'merge carefully.': 'ശ്രദ്ധിച്ച് റോഡിലേക്ക് ചേരുക.',
'Downhill ahead.': 'മുന്നിൽ ഇറക്കമുണ്ട്.',
'Steep downhill ahead.': 'മുന്നിൽ കുത്തനെയുള്ള ഇറക്കമുണ്ട്.',
'Uphill ahead.': 'മുന്നിൽ കയറ്റമുണ്ട്.',
'Steep uphill ahead.': 'മുന്നിൽ കുത്തനെയുള്ള കയറ്റമുണ്ട്.',
'Do not coast in neutral.': 'ന്യൂട്രലിൽ ഇട്ട് ഇറക്കരുത്.',
'Control your speed with the brake.': 'ബ്രേക്ക് ഉപയോഗിച്ച് വേഗം നിയന്ത്രിക്കുക.',
'Change down a gear.': 'ഒരു ഗിയർ കുറയ്ക്കുക.',
'Left bend ahead.': 'മുന്നിൽ ഇടത്തേക്ക് വളവുണ്ട്.',
'Right bend ahead.': 'മുന്നിൽ വലത്തേക്ക് വളവുണ്ട്.',
'Sharp left bend ahead.': 'മുന്നിൽ ഇടത്തേക്ക് കൊടുംവളവുണ്ട്.',
'Sharp right bend ahead.': 'മുന്നിൽ വലത്തേക്ക് കൊടുംവളവുണ്ട്.',
# ---- whole sentences (ld::Prompt, ids 1..95)
'Left turn ahead. Check your mirrors and signal left': 'മുന്നിൽ ഇടത്തേക്ക് തിരിയണം. മിററുകൾ നോക്കി ഇടത്തേക്ക് ഇൻഡിക്കേറ്റർ ഇടുക',
'Right turn ahead. Check your mirrors and signal right': 'മുന്നിൽ വലത്തേക്ക് തിരിയണം. മിററുകൾ നോക്കി വലത്തേക്ക് ഇൻഡിക്കേറ്റർ ഇടുക',
'Junction ahead. Check your mirrors and ease off the accelerator': 'മുന്നിൽ ജംഗ്ഷനുണ്ട്. മിററുകൾ നോക്കി ആക്സിലറേറ്റർ കുറയ്ക്കുക',
'Ease off the accelerator and brake gently. Change down to second as you slow': 'ആക്സിലറേറ്റർ കുറച്ച് പതുക്കെ ബ്രേക്ക് ചവിട്ടുക. വേഗം കുറയുമ്പോൾ ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റുക',
'Change down to second gear and cover the brake': 'ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റി കാൽ ബ്രേക്കിന് മുകളിൽ വെക്കുക',
'Clutch down if you need to stop. Look both ways before you turn': 'നിർത്തണമെങ്കിൽ ക്ലച്ച് ചവിട്ടുക. തിരിയുന്നതിന് മുമ്പ് ഇരുവശവും നോക്കുക',
'Straighten the wheel and add gentle accelerator': 'സ്റ്റിയറിംഗ് നേരെയാക്കി പതുക്കെ ആക്സിലറേറ്റർ കൊടുക്കുക',
'Person ahead. Ease off the accelerator and cover the brake': 'മുന്നിൽ ആളുണ്ട്. ആക്സിലറേറ്റർ കുറച്ച് കാൽ ബ്രേക്കിന് മുകളിൽ വെക്കുക',
'Two-wheeler ahead. Ease off the accelerator and cover the brake': 'മുന്നിൽ ടൂവീലറുണ്ട്. ആക്സിലറേറ്റർ കുറച്ച് കാൽ ബ്രേക്കിന് മുകളിൽ വെക്കുക',
'Vehicle ahead. Ease off and keep a safe gap': 'മുന്നിൽ വാഹനമുണ്ട്. ആക്സിലറേറ്റർ കുറച്ച് സുരക്ഷിതമായ അകലം പാലിക്കുക',
'Brake gently and be ready to stop': 'പതുക്കെ ബ്രേക്ക് ചവിട്ടി നിർത്താൻ തയ്യാറാവുക',
'Clutch down and hold the brake': 'ക്ലച്ചും ബ്രേക്കും ചവിട്ടിപ്പിടിക്കുക',
'Change up to second gear': 'ഗിയർ കൂട്ടി സെക്കൻഡിലേക്ക് മാറ്റുക',
'Change up to third gear': 'ഗിയർ കൂട്ടി തേർഡിലേക്ക് മാറ്റുക',
'Change up to fourth gear': 'ഗിയർ കൂട്ടി ഫോർത്തിലേക്ക് മാറ്റുക',
'Change up to fifth gear': 'ഗിയർ കൂട്ടി ഫിഫ്ത്തിലേക്ക് മാറ്റുക',
'Change down to second gear': 'ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റുക',
'Change down to third gear': 'ഗിയർ കുറച്ച് തേർഡിലേക്ക് മാറ്റുക',
'Clutch down as you stop': 'നിർത്തുമ്പോൾ ക്ലച്ച് ചവിട്ടുക',
'When it is clear: first gear, check mirrors and signal, then release the clutch slowly': 'വഴി ഒഴിയുമ്പോൾ: ഫസ്റ്റ് ഗിയറിടുക, മിററുകൾ നോക്കി ഇൻഡിക്കേറ്റർ ഇടുക, എന്നിട്ട് ക്ലച്ച് പതുക്കെ വിടുക',
'Downhill. Change down to third and control your speed with the brake': 'ഇറക്കമാണ്. ഗിയർ കുറച്ച് തേർഡിലേക്ക് മാറ്റി, ബ്രേക്ക് ഉപയോഗിച്ച് വേഗം നിയന്ത്രിക്കുക',
'Steep downhill. Change down to second and control your speed with the brake': 'കുത്തനെയുള്ള ഇറക്കമാണ്. ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റി, ബ്രേക്ക് ഉപയോഗിച്ച് വേഗം നിയന്ത്രിക്കുക',
'Uphill. Change down to second and keep a steady accelerator': 'കയറ്റമാണ്. ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റി, ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക',
'Uphill. Change down to third and keep a steady accelerator': 'കയറ്റമാണ്. ഗിയർ കുറച്ച് തേർഡിലേക്ക് മാറ്റി, ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക',
'Vehicle crossing ahead. Brake and wait': 'മുന്നിൽ വാഹനം കുറുകെ കടക്കുന്നു. ബ്രേക്ക് ചവിട്ടി കാത്തുനിൽക്കുക',
'Person crossing ahead. Stop and let them cross': 'മുന്നിൽ ആൾ റോഡ് മുറിച്ചുകടക്കുന്നു. നിർത്തി അവരെ കടന്നുപോകാൻ അനുവദിക്കുക',
'Oncoming vehicle. Keep left and ease off': 'എതിരെ വാഹനം വരുന്നു. ഇടത്തോട്ട് ഒതുക്കി ആക്സിലറേറ്റർ കുറയ്ക്കുക',
'Person on the left. Ease off and give them space': 'ഇടതുവശത്ത് ആളുണ്ട്. ആക്സിലറേറ്റർ കുറച്ച് അവർക്ക് ഇടം കൊടുക്കുക',
'Person on the right. Ease off and give them space': 'വലതുവശത്ത് ആളുണ്ട്. ആക്സിലറേറ്റർ കുറച്ച് അവർക്ക് ഇടം കൊടുക്കുക',
'It has passed. Look both ways again before you move off': 'അത് കടന്നുപോയി. നീങ്ങുന്നതിന് മുമ്പ് ഇരുവശവും ഒന്നുകൂടി നോക്കുക',
'You are drifting left. Move back to the centre of your lane': 'വണ്ടി ഇടത്തോട്ട് നീങ്ങുന്നുണ്ട്. ലെയ്നിന്റെ നടുവിലേക്ക് തിരികെ വരിക',
'You are drifting right. Keep left': 'വണ്ടി വലത്തോട്ട് നീങ്ങുന്നുണ്ട്. ഇടതുവശം ചേർന്ന് പോകുക',
'Speed bump ahead. Slow down, clutch down and change down': 'മുന്നിൽ ഹമ്പുണ്ട്. വേഗം കുറച്ച്, ക്ലച്ച് ചവിട്ടി ഗിയർ കുറയ്ക്കുക',
'Sharp bend ahead. Sound the horn and keep left': 'മുന്നിൽ കൊടുംവളവുണ്ട്. ഹോൺ അടിച്ച് ഇടതുവശം ചേർന്ന് പോകുക',
'The route has changed. Keep going and follow the new route': 'റൂട്ട് മാറി. യാത്ര തുടർന്ന് പുതിയ റൂട്ട് പിന്തുടരുക',
'Downhill ahead. Stay in gear and control your speed with the brake': 'മുന്നിൽ ഇറക്കമുണ്ട്. ഗിയറിൽ തന്നെ ഇറക്കി, ബ്രേക്ക് ഉപയോഗിച്ച് വേഗം നിയന്ത്രിക്കുക',
'Steep downhill ahead. Change down to second and control your speed with the brake': 'മുന്നിൽ കുത്തനെയുള്ള ഇറക്കമുണ്ട്. ഗിയർ കുറച്ച് സെക്കൻഡിലേക്ക് മാറ്റി, ബ്രേക്ക് ഉപയോഗിച്ച് വേഗം നിയന്ത്രിക്കുക',
'Uphill ahead. Keep a steady accelerator': 'മുന്നിൽ കയറ്റമുണ്ട്. ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക',
'Steep uphill ahead. Change down before the climb and keep a steady accelerator': 'മുന്നിൽ കുത്തനെയുള്ള കയറ്റമുണ്ട്. കയറ്റത്തിന് മുമ്പ് ഗിയർ കുറച്ച്, ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക',
'Set a suitable approach speed. Prepare to give way': 'വേഗം കുറച്ച് സമീപിക്കുക. വഴി കൊടുക്കാൻ തയ്യാറാവുക',
'Uphill. Lower gear if needed': 'കയറ്റമാണ്. ആവശ്യമെങ്കിൽ ഗിയർ കുറയ്ക്കുക',
'Ease off accelerator': 'ആക്സിലറേറ്റർ കുറയ്ക്കുക',
'Brake gently': 'പതുക്കെ ബ്രേക്ക് ചവിട്ടുക',
'Collision risk. Brake now': 'ഇടിക്കാൻ സാധ്യത. ഉടനെ ബ്രേക്ക് ചവിട്ടുക',
'Camera is on. Watch the road and keep a safe gap': 'ക്യാമറ ഓണാണ്. റോഡ് ശ്രദ്ധിച്ച് സുരക്ഷിതമായ അകലം പാലിക്കുക',
'Obstacle in your path. Slow down and be ready to stop': 'വഴിയിൽ തടസ്സമുണ്ട്. വേഗം കുറച്ച് നിർത്താൻ തയ്യാറാവുക',
'Road user ahead. Slow down and give them space': 'മുന്നിൽ ആളോ വാഹനമോ ഉണ്ട്. വേഗം കുറച്ച് ഇടം കൊടുക്കുക',
'Traffic ahead. Keep a safe gap': 'മുന്നിൽ ട്രാഫിക്കുണ്ട്. സുരക്ഷിതമായ അകലം പാലിക്കുക',
'Person ahead. Give space': 'മുന്നിൽ ആളുണ്ട്. ഇടം കൊടുക്കുക',
'Two-wheeler ahead. Give space': 'മുന്നിൽ ടൂവീലറുണ്ട്. ഇടം കൊടുക്കുക',
'Vehicle ahead. Keep a gap': 'മുന്നിൽ വാഹനമുണ്ട്. അകലം പാലിക്കുക',
'Left bend ahead. Prepare early': 'മുന്നിൽ ഇടത്തേക്ക് വളവുണ്ട്. നേരത്തെ തയ്യാറാവുക',
'Right bend ahead. Prepare early': 'മുന്നിൽ വലത്തേക്ക് വളവുണ്ട്. നേരത്തെ തയ്യാറാവുക',
'Junction ahead. Check for traffic': 'മുന്നിൽ ജംഗ്ഷനുണ്ട്. വാഹനങ്ങൾ വരുന്നുണ്ടോ എന്ന് നോക്കുക',
'Left turn ahead. Check for traffic': 'മുന്നിൽ ഇടത്തേക്ക് തിരിയണം. വാഹനങ്ങൾ വരുന്നുണ്ടോ എന്ന് നോക്കുക',
'Right turn ahead. Check for traffic': 'മുന്നിൽ വലത്തേക്ക് തിരിയണം. വാഹനങ്ങൾ വരുന്നുണ്ടോ എന്ന് നോക്കുക',
'Left turn in about ten metres': 'ഏകദേശം പത്ത് മീറ്റർ കഴിഞ്ഞ് ഇടത്തേക്ക് തിരിയണം',
'Right turn in about ten metres': 'ഏകദേശം പത്ത് മീറ്റർ കഴിഞ്ഞ് വലത്തേക്ക് തിരിയണം',
'Left turn in about fifty metres': 'ഏകദേശം അമ്പത് മീറ്റർ കഴിഞ്ഞ് ഇടത്തേക്ക് തിരിയണം',
'Right turn in about fifty metres': 'ഏകദേശം അമ്പത് മീറ്റർ കഴിഞ്ഞ് വലത്തേക്ക് തിരിയണം',
'Uphill. Steady power': 'കയറ്റമാണ്. ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക',
'Downhill. Control speed': 'ഇറക്കമാണ്. വേഗം നിയന്ത്രിക്കുക',
'Left bend ahead. Ease off early and stay in your lane': 'മുന്നിൽ ഇടത്തേക്ക് വളവുണ്ട്. നേരത്തെ ആക്സിലറേറ്റർ കുറച്ച് ലെയ്നിൽ തന്നെ തുടരുക',
'Right bend ahead. Ease off early and stay in your lane': 'മുന്നിൽ വലത്തേക്ക് വളവുണ്ട്. നേരത്തെ ആക്സിലറേറ്റർ കുറച്ച് ലെയ്നിൽ തന്നെ തുടരുക',
'Downhill. Use engine braking': 'ഇറക്കമാണ്. എഞ്ചിൻ ബ്രേക്കിംഗ് ഉപയോഗിക്കുക',
'Engine revs are high. Shift up one gear when safe': 'എഞ്ചിൻ റേസ് ആകുന്നുണ്ട്. സുരക്ഷിതമാകുമ്പോൾ ഒരു ഗിയർ കൂട്ടുക',
'Engine revs are low under load. Shift down one gear when safe': 'എഞ്ചിൻ വലിക്കുന്നില്ല. സുരക്ഷിതമാകുമ്പോൾ ഒരു ഗിയർ കുറയ്ക്കുക',
'Obstacle ahead. Keep space and be ready to stop': 'മുന്നിൽ തടസ്സമുണ്ട്. അകലം വിട്ട് നിർത്താൻ തയ്യാറാവുക',
'Obstacle ahead. Keep space. Select a lower gear only if the engine struggles': 'മുന്നിൽ തടസ്സമുണ്ട്. അകലം വിടുക. എഞ്ചിൻ വലിക്കുന്നില്ലെങ്കിൽ മാത്രം ഗിയർ കുറയ്ക്കുക',
'Road user ahead. Give them space': 'മുന്നിൽ ആളോ വാഹനമോ ഉണ്ട്. ഇടം കൊടുക്കുക',
'Collision risk nearby. Stay alert': 'അടുത്ത് ഇടിക്കാൻ സാധ്യതയുണ്ട്. ശ്രദ്ധിക്കുക',
'Left bend ahead. Stay in your lane and look through the bend': 'മുന്നിൽ ഇടത്തേക്ക് വളവുണ്ട്. ലെയ്നിൽ തന്നെ തുടർന്ന് വളവിനപ്പുറത്തേക്ക് നോക്കുക',
'Right bend ahead. Stay in your lane and look through the bend': 'മുന്നിൽ വലത്തേക്ക് വളവുണ്ട്. ലെയ്നിൽ തന്നെ തുടർന്ന് വളവിനപ്പുറത്തേക്ക് നോക്കുക',
'Left bend ahead. Set your speed and gear before the bend. Stay in your lane': 'മുന്നിൽ ഇടത്തേക്ക് വളവുണ്ട്. വളവിന് മുമ്പ് വേഗവും ഗിയറും ശരിയാക്കുക. ലെയ്നിൽ തന്നെ തുടരുക',
'Right bend ahead. Set your speed and gear before the bend. Stay in your lane': 'മുന്നിൽ വലത്തേക്ക് വളവുണ്ട്. വളവിന് മുമ്പ് വേഗവും ഗിയറും ശരിയാക്കുക. ലെയ്നിൽ തന്നെ തുടരുക',
'Obstacle ahead. Slow down smoothly. Change down only if the engine struggles as you slow': 'മുന്നിൽ തടസ്സമുണ്ട്. പതുക്കെ വേഗം കുറയ്ക്കുക. വേഗം കുറയുമ്പോൾ എഞ്ചിൻ വലിക്കുന്നില്ലെങ്കിൽ മാത്രം ഗിയർ കുറയ്ക്കുക',
'Obstacle ahead. Keep a safe gap and be ready to stop': 'മുന്നിൽ തടസ്സമുണ്ട്. സുരക്ഷിതമായ അകലം പാലിച്ച് നിർത്താൻ തയ്യാറാവുക',
'Obstacle ahead. Keep a safe gap. Change down only if the engine struggles': 'മുന്നിൽ തടസ്സമുണ്ട്. സുരക്ഷിതമായ അകലം പാലിക്കുക. എഞ്ചിൻ വലിക്കുന്നില്ലെങ്കിൽ മാത്രം ഗിയർ കുറയ്ക്കുക',
'Gentle uphill. Steady power': 'ചെറിയ കയറ്റമാണ്. ആക്സിലറേറ്റർ ഒരേപോലെ പിടിക്കുക',
'Gentle downhill. Watch speed': 'ചെറിയ ഇറക്കമാണ്. വേഗം ശ്രദ്ധിക്കുക',
'Steep uphill. Avoid engine strain': 'കുത്തനെയുള്ള കയറ്റമാണ്. എഞ്ചിന് ആയാസം വരാതെ നോക്കുക',
'Steep downhill. Suitable low gear': 'കുത്തനെയുള്ള ഇറക്കമാണ്. താഴ്ന്ന ഗിയറിൽ ഇറക്കുക',
'Steep downhill. Control speed': 'കുത്തനെയുള്ള ഇറക്കമാണ്. വേഗം നിയന്ത്രിക്കുക',
'Very steep uphill. Controlled power': 'വളരെ കുത്തനെയുള്ള കയറ്റമാണ്. ശ്രദ്ധിച്ച് ആക്സിലറേറ്റർ കൊടുക്കുക',
'Very steep downhill. Brake as needed': 'വളരെ കുത്തനെയുള്ള ഇറക്കമാണ്. ആവശ്യത്തിന് ബ്രേക്ക് ചവിട്ടുക',
'Slope reading unavailable': 'ചരിവ് അളക്കാൻ കഴിയുന്നില്ല',
'Maintain speed': 'ഇതേ വേഗത്തിൽ തുടരുക',
'Hold lane and distance': 'ലെയ്നും അകലവും പാലിക്കുക',
'Keep a safe gap': 'സുരക്ഷിതമായ അകലം പാലിക്കുക',
'Slow down smoothly': 'പതുക്കെ വേഗം കുറയ്ക്കുക',
'Watch for pedestrians': 'കാൽനടക്കാരെ ശ്രദ്ധിക്കുക',
'Prepare for the bend': 'വളവിനായി തയ്യാറാവുക',
'Observe the road sign': 'ട്രാഫിക് ബോർഡ് ശ്രദ്ധിക്കുക',
'Brake harder': 'കൂടുതൽ ശക്തിയായി ബ്രേക്ക് ചവിട്ടുക',
'Emergency stop': 'ഉടനെ നിർത്തുക',
}

missing = [t for t in fragments + prompts if t not in ML]
extra = [t for t in ML if t not in fragments + prompts]
if missing or extra:
    sys.exit('missing: %r\nextra: %r' % (missing, extra))
for group, name in ((fragments, 'phrases'), (prompts, 'sentences')):
    words = [ML[t] for t in group]
    dupes = {w for w in words if words.count(w) > 1}
    if dupes: sys.exit('two %s share Malayalam words: %r' % (name, dupes))

def kt(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('$', '\\$') + '"'
lines = []
lines.append('        // Phrases (ld::Fragment 1..%d), in id order; the core joins them into one sentence per situation.' % len(fragments))
lines += ['        %s to %s,' % (kt(t), kt(ML[t])) for t in fragments]
lines.append('        // Whole sentences (ld::Prompt, %d), in the order of ld::promptText.' % len(prompts))
lines += ['        %s to %s,' % (kt(t), kt(ML[t])) for t in prompts]
lines[-1] = lines[-1].rstrip(',')

out = root / 'android/app/src/main/java/ai/learndrive/mobile/audio/VoicePhrases.kt'
out.write_text('''package ai.learndrive.mobile.audio

/**
 * The coach's words in each voice language. English is the core's own text (ld::promptText and
 * ld::fragmentText); other languages are keyed by it, so a sentence the core composes from phrases
 * is spoken from the same phrases in the same order. The phone's own voice for that language speaks
 * them ([PhoneVoice]); until it has rendered every one, the bundled English recordings speak.
 *
 * Malayalam keeps the car's controls as Kerala instructors say them, in Malayalam script (brake,
 * clutch, gear, accelerator, indicator, horn, mirror), and numbers as words so every voice reads
 * them the same way. Generated by tools/voice_phrases_ml.py from its table; edit the table there.
 */
object VoicePhrases {
    const val ENGLISH="en"
    const val MALAYALAM="ml"
    /** The voice languages offered: code and the name a learner reads. */
    val languages=listOf(ENGLISH to "English",MALAYALAM to "മലയാളം · Malayalam")
    fun supported(language:String?)=languages.any {it.first==language}
    /** English name of a voice language, for status lines. */
    fun name(language:String)=if(language==MALAYALAM)"Malayalam" else "English"
    /** The words for [english] in [language]; null when there are none (the English recording then speaks). */
    fun spoken(language:String,english:String):String? {
        val key=english.trim()
        return when(language){ENGLISH->key;MALAYALAM->MALAYALAM_WORDS[key];else->null}
    }
    internal val MALAYALAM_WORDS:Map<String,String> = mapOf(
''' + '\n'.join(lines) + '''
    )
}
''', encoding='utf-8', newline='\n')
print('wrote', out, len(fragments), 'phrases', len(prompts), 'sentences')
