# See LICENSE file for copyright and license details.
project('dwm', 'c', version: '6.1.0')

git=run_command('git', 'describe', '--tags', check: false)
if git.returncode() != 0
  error('Version cannot be determine')
endif
version=git.stdout().strip()

c = meson.get_compiler('c')
Xlib = c.find_library('X11')
Xlib_xcb = c.find_library('X11-xcb')
xcb = c.find_library('xcb')
xcb_res = c.find_library('xcb-res')

Xft = dependency('xft')
fontconfig = dependency('fontconfig')

Xinerama = c.find_library('Xinerama')
m = c.find_library('m', required: false)

add_project_arguments('-DXINERAMA', language: 'c')

conf_data = configuration_data()
conf_data.set('version', version)

tagbar_lang = get_option('TAGBAR_LANG')
if tagbar_lang == 'latin'
  conf_data.set('tags', '{ "1", "2", "3", "4", "5", "6", "7", "8", "9" }')
elif tagbar_lang == 'roman'
  conf_data.set('tags', '{ "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" }')
elif tagbar_lang == 'japanese'
  conf_data.set('tags', '{ "一", "二", "三", "四", "五", "六", "七", "八", "九" }')
else
  error('Unsopported tagbar_lang ' + tagbar_lang)
endif

terminal_app = get_option('TERMINAL_APP')
if terminal_app != ''
  conf_data.set_quoted('USE_TERMINAL', terminal_app)
endif

lockscreen_app = get_option('LOCKSCREEN_APP')
if lockscreen_app != ''
  conf_data.set_quoted('USE_LOCKSCREEN', lockscreen_app)
endif

conf_data.set('USE_EXPLORER', get_option('USE_EXPLORER'))
conf_data.set('USE_SCROT', get_option('USE_SCROT'))


configure_file(input: 'config.h.in',
    output: 'config.h',
    configuration: conf_data)

executable('dwm', ['dwm.c', 'drw.c', 'util.c'],
    dependencies: [Xlib, Xlib_xcb, xcb, xcb_res, Xinerama, Xft, fontconfig, m],
    install: true)

#extra_includes = ['/usr/X11R6/include']
#extra_lib = ['/usr/X11R6/lib']


