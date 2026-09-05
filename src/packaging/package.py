# -*- coding: utf-8 -*-

import jinja2
import os
import re
from datetime import date

ROOT = os.path.dirname(__file__)

def main():
    # grab_version
    version = grab_version()

    # set names and folders
    variables = {
        'publisher': 'retrohacks.nl',
        'progfolder': 'p2000t-cartridge-studio',
        'executable': 'p2000t-cartridge-studio',
        'artifactbasename': 'p2000t-cartridge-studio',
        'progdescription': 'P2000T Cartridge Studio',
        'progname': 'P2000T Cartridge Studio',
        'version': version,
        'releasedate': str(date.today()),
        'iconfilename': 'p2000t-cartridge-studio.ico',
    }

    templates, filenames = read_templates()
    render(templates, filenames, variables)

def render(templates, filenames, variables):
    for template, filename in zip(templates, filenames):
        content = template.render(variables)
        print('Writing to: %s' % filename)
        with open(filename, 'w', encoding='utf-8') as f:
            f.write(content)

def read_templates():
    templates = [
        os.path.join(ROOT, 'setup.iss'),
    ]

    jinja_templates = []
    for t in templates:
        e = jinja2.Environment()
        with open(t + '.template') as f:
            jinja_templates.append(e.from_string(f.read()))

    return jinja_templates, templates

def grab_version():
    version_file = os.path.join(ROOT, '..', '..', 'VERSION')
    with open(version_file, encoding='utf-8') as f:
        version = f.read().strip()
    if not re.fullmatch(r'[0-9]\.[0-9]\.[0-9]', version):
        raise ValueError('VERSION must contain three single-digit components such as 0.1.0')
    return version

if __name__ == '__main__':
    main()
