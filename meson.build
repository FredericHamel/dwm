# See LICENSE file for copyright and license details.
project('dwm', 'c')

git=run_command('git', 'describe', '--tags')
if git.returncode() != 0
  error('Version cannot be determine')
endif
version=git.stdout().strip()

c = meson.get_compiler('c')
Xlib = c.find_library('X11')
Xinerama = c.find_library('Xinerama')
m = c.find_library('m')

add_project_arguments('-DXINERAMA', language: 'c')

conf_data = configuration_data()
conf_data.set('version', version)

tagbar_lang = get_option('tagbar_lang')
if tagbar_lang == 'latin'
  conf_data.set('tags', '{ "1", "2", "3", "4", "5", "6", "7", "8", "9" }')
elif tagbar_lang == 'roman'
  conf_data.set('tags', '{ "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" }')
elif tagbar_lang == 'japanese'
  conf_data.set('tags', '{ "一", "二", "三", "四", "五", "六", "七", "八", "九" }')
else
  error('Unsopported tagbar_lang ' + tagbar_lang)
endif

conf_data.set('terminal_app', get_option('terminal_app'))
conf_data.set('lockscreen_app', get_option('lockscreen_app'))

configure_file(input: 'config.h.in',
    output: 'config.h',
    configuration: conf_data)

executable('dwm', ['dwm.c'],
    dependencies: [Xlib, Xinerama, m])

#extra_includes = ['/usr/X11R6/include']
#extra_lib = ['/usr/X11R6/lib']


