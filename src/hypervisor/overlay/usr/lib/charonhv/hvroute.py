# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the ONE browser layer for old guests (6u). The guest opens http://simple.charon/ (one entry
point); for every address the hypervisor decides where the page is made:

  simple  - ordinary pages: fetched with modern TLS and given as light HTML 4.01 (hvsimple);
  remote  - script-heavy sites (Facebook, Instagram, X, LinkedIn, web mail ...): the remote modern browser renders the
            page and the guest gets its accessibility tree as HTML with real controls (hvremote); their terms
            forbid automated access - the page and the voice say so;
  modern  - video: DRM services (Netflix ...) AND YouTube-like sites. DRM video is never passed into a guest. YouTube's
            Terms of Service (checked 2026-11-25, work/r410-hv-research-6u-6z.txt) say: "You are not allowed to:
            access, reproduce, download, distribute, transmit, broadcast, display, sell, license, alter, modify or
            otherwise use any part of the Service or any Content except: (a) as expressly authorized by the Service;
            or (b) with prior written permission from YouTube and, where applicable, respective rights holders" and
            forbid access "using any automated means (such as robots, botnets or scrapers)". So Charon does NOT fetch
            and transcode YouTube video for the old player (6u's proposed ffmpeg route is NOT built); such video
            plays in the hypervisor's modern browser on the real screen (6s), which the page and the voice offer."""
import urllib.parse

REMOTE_SITES = ('facebook.com', 'instagram.com', 'x.com', 'twitter.com', 'linkedin.com', 'threads.net', 'tiktok.com',
                'mail.google.com', 'outlook.live.com', 'web.whatsapp.com', 'messenger.com', 'reddit.com')
VIDEO_SITES = ('youtube.com', 'youtu.be', 'youtube-nocookie.com', 'vimeo.com', 'dailymotion.com', 'twitch.tv')
DRM_SITES = ('netflix.com', 'disneyplus.com', 'primevideo.com', 'max.com', 'hbomax.com', 'spotify.com', 'canalplus.com',
             'player.pl', 'polsatboxgo.pl', 'skyshowtime.com', 'tv.apple.com', 'crunchyroll.com', 'paramountplus.com')


def site_of(host, sites):
    host = (host or '').lower().rstrip('.')
    for s in sites:
        if host == s or host.endswith('.' + s):
            return s
    return None


def route(url):
    """('simple' | 'remote' | 'modern', the site matched or '', 'drm' | 'video' | '')."""
    host = urllib.parse.urlsplit(url if '//' in url else 'http://' + url).hostname
    s = site_of(host, DRM_SITES)
    if s:
        return 'modern', s, 'drm'
    s = site_of(host, VIDEO_SITES)
    if s:
        return 'modern', s, 'video'
    s = site_of(host, REMOTE_SITES)
    if s:
        return 'remote', s, ''
    return 'simple', '', ''


def modern_page(url, site, why):
    import html
    if why == 'drm':
        en = ('%s plays protected video (DRM). Protected video is never passed into this old Windows. It plays in the modern '
              'browser of Charon Hypervisor on the real screen: the hypervisor has said how to open it.' % site)
        pl = ('%s odtwarza chronione wideo (DRM). Chronione wideo nigdy nie trafia do tego starego Windows. Zagra w nowoczesnej '
              'przeglądarce Charon Hypervisor na prawdziwym ekranie: hipernadzorca powiedział, jak ją otworzyć.' % site)
    else:
        en = ("%s's terms forbid downloading or converting its videos except where the site itself offers it, so Charon does "
              'not convert them for this old player. The video plays in the modern browser of Charon Hypervisor on the real '
              'screen: the hypervisor has said how to open it.' % site)
        pl = ('Regulamin %s zabrania pobierania i przerabiania filmów poza tym, co oferuje sam serwis, więc Charon nie przerabia '
              'ich dla tego starego odtwarzacza. Film zagra w nowoczesnej przeglądarce Charon Hypervisor na prawdziwym ekranie: '
              'hipernadzorca powiedział, jak ją otworzyć.' % site)
    return ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">\n<html><head><meta http-equiv="Content-Type" '
            'content="text/html; charset=utf-8"><title>Charon Hypervisor: %s</title></head><body><h1>%s</h1><p>%s</p>'
            '<p lang="pl">%s</p><p>%s</p><p><a href="/">Charon simple web</a></p></body></html>\n'
            % (html.escape(site), html.escape(site), html.escape(en), html.escape(pl), html.escape(url))).encode('utf-8')
