// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for src/efi/CharonHtmlText.h.
// Usage: CharonHtmlTextTests [fuzz iterations (default 4000)] [file.html ...]
// Every file given is rendered and dumped (kind, level, flags, text) after the
// built-in tests; handy for looking at a real page.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonHtmlText.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace hakefi::html;

static int gFailures = 0;
static int gChecks = 0;

static void Check(bool ok, const char* what)
{
	gChecks++;
	if (!ok) {
		printf("FAIL %s\n", what);
		gFailures++;
	}
}

static void Section(const char* name) { printf("---- %s\n", name); }

static std::string Utf8(const char16_t* text, size_t length)
{
	std::string out;
	for (size_t i = 0; i < length; i++) {
		uint32_t c = text[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && text[i + 1] >= 0xDC00
			&& text[i + 1] < 0xE000)
			c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
		if (c < 0x80) out += char(c);
		else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 63)); }
		else if (c < 0x10000) {
			out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63));
			out += char(0x80 | (c & 63));
		} else {
			out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63));
			out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
		}
	}
	return out;
}

// A page with heap buffers of exactly the requested sizes (so that the address
// sanitizer sees any write or read past them).
struct Doc {
	std::vector<char16_t> text;
	std::vector<Line> lines;
	std::vector<Link> links;
	std::vector<char> strings;
	std::vector<Form> forms;
	std::vector<Field> fields;
	std::vector<Anchor> anchors;
	Page page{};
	Options options;
	bool complete = false;

	explicit Doc(uint32_t textN = 1 << 16, uint32_t lineN = 4096, uint32_t linkN = 1024,
		uint32_t stringN = 1 << 16, uint32_t formN = 16, uint32_t fieldN = 256,
		uint32_t anchorN = 256)
		: text(textN), lines(lineN), links(linkN), strings(stringN), forms(formN),
		fields(fieldN), anchors(anchorN)
	{
		page.text = textN ? text.data() : nullptr; page.textCapacity = textN;
		page.lines = lineN ? lines.data() : nullptr; page.lineCapacity = lineN;
		page.links = linkN ? links.data() : nullptr; page.linkCapacity = linkN;
		page.strings = stringN ? strings.data() : nullptr; page.stringCapacity = stringN;
		page.forms = formN ? forms.data() : nullptr; page.formCapacity = formN;
		page.fields = fieldN ? fields.data() : nullptr; page.fieldCapacity = fieldN;
		page.anchors = anchorN ? anchors.data() : nullptr; page.anchorCapacity = anchorN;
	}

	bool Html(const std::string& html, Charset hint = Charset::Auto)
	{
		// exact-size copy: reads past the end are caught by the sanitizer
		std::vector<uint8_t> copy(html.begin(), html.end());
		complete = Render(copy.data(), copy.size(), hint, page, options);
		return complete;
	}
	bool Plain(const std::string& text_, Charset hint = Charset::Auto)
	{
		std::vector<uint8_t> copy(text_.begin(), text_.end());
		complete = RenderPlain(copy.data(), copy.size(), hint, page, options);
		return complete;
	}
	std::string LineText(uint32_t i) const
	{
		return i < page.lineCount ? Utf8(page.text + page.lines[i].start, page.lines[i].length)
			: std::string("<no line>");
	}
	std::string String(uint32_t start, uint32_t length) const
	{
		return std::string(page.strings + start, length);
	}
	std::string LinkUrl(uint32_t i) const { return String(page.links[i].urlStart, page.links[i].urlLength); }
	std::string LinkText(uint32_t i) const
	{
		return Utf8(page.text + page.links[i].textStart, page.links[i].textLength);
	}
	std::string Title() const { return Utf8(page.title, page.titleLength); }
	void Dump() const
	{
		static const char* kinds[] = {"text", "head", "item", "link", "form", "pre", "row",
			"quote", "rule", "image"};
		for (uint32_t i = 0; i < page.lineCount; i++) {
			const Line& l = page.lines[i];
			printf("  %3u %-5s L%u f%02x links %u@%d field %d |%s|\n", i,
				l.kind < 10 ? kinds[l.kind] : "?", l.level, l.flags, l.linkCount,
				int(l.firstLink), l.field == kNoField ? -1 : int(l.field), LineText(i).c_str());
		}
	}
};

// Structural invariants that must hold for every page, truncated or not.
static bool Valid(const Doc& d, std::string* why = nullptr)
{
	const Page& g = d.page;
	auto bad = [&](const char* what) {
		if (why) *why = what;
		return false;
	};
	const uint32_t width = d.options.width && d.options.width < 16 ? 16 : d.options.width;
	if (g.textCount > g.textCapacity || g.lineCount > g.lineCapacity
		|| g.linkCount > g.linkCapacity || g.stringCount > g.stringCapacity
		|| g.formCount > g.formCapacity || g.fieldCount > g.fieldCapacity
		|| g.anchorCount > g.anchorCapacity)
		return bad("count above capacity");
	if (g.truncated != (((g.overflow & ~overflowAnchors) != 0)))
		return bad("truncated does not match overflow bits");
	if (g.titleLength >= kTitleChars || g.title[g.titleLength] != 0)
		return bad("title");
	if (uint64_t(g.baseStart) + g.baseLength > g.stringCount)
		return bad("base range");
	uint32_t end = 0, nextLink = 0;
	for (uint32_t i = 0; i < g.lineCount; i++) {
		const Line& l = g.lines[i];
		if (l.start < end || uint64_t(l.start) + l.length > g.textCount)
			return bad("line range");
		if (l.start > end + 1)
			return bad("more than one separator between lines");
		end = l.start + l.length;
		if (width && l.length > width)
			return bad("line longer than width");
		if (l.kind > kindImage)
			return bad("line kind");
		if (l.kind == kindForm && l.field != kNoField && l.field >= g.fieldCount)
			return bad("line field");
		if (l.kind != kindForm && l.field != kNoField)
			return bad("field on a non-form line");
		if (l.linkCount) {
			if (l.firstLink != nextLink)
				return bad("firstLink out of order");
			for (uint32_t k = 0; k < l.linkCount; k++) {
				if (nextLink >= g.linkCount || g.links[nextLink].line != i)
					return bad("link line mismatch");
				const Link& link = g.links[nextLink];
				if (link.textStart < l.start || link.textStart > l.start + l.length)
					return bad("link text does not start on its line");
				nextLink++;
			}
		} else if (l.firstLink != kNone) {
			return bad("firstLink without links");
		}
		for (uint32_t k = 0; k < l.length; k++)
			if (g.text[l.start + k] == u'\n' || g.text[l.start + k] < 0x20)
				return bad("control character in a line");
	}
	if (nextLink != g.linkCount)
		return bad("links not all on lines");
	for (uint32_t i = 0; i < g.linkCount; i++) {
		const Link& l = g.links[i];
		if (uint64_t(l.urlStart) + l.urlLength > g.stringCount)
			return bad("link url range");
		if (uint64_t(l.textStart) + l.textLength > g.textCount)
			return bad("link text range");
		if (l.line >= g.lineCount)
			return bad("link line");
	}
	for (uint32_t i = 0; i < g.formCount; i++) {
		const Form& f = g.forms[i];
		if (uint64_t(f.actionStart) + f.actionLength > g.stringCount)
			return bad("form action range");
		if (f.fieldCount && uint64_t(f.firstField) + f.fieldCount > g.fieldCount)
			return bad("form field run");
		if (f.method > 1)
			return bad("form method");
	}
	for (uint32_t i = 0; i < g.fieldCount; i++) {
		const Field& f = g.fields[i];
		if (uint64_t(f.nameStart) + f.nameLength > g.stringCount
			|| uint64_t(f.valueStart) + f.valueLength > g.stringCount
			|| uint64_t(f.labelStart) + f.labelLength > g.stringCount)
			return bad("field string range");
		if (f.form != kNoField && f.form >= g.formCount)
			return bad("field form");
		if (f.line != kNone && f.line >= g.lineCount)
			return bad("field line");
		if (f.type > fieldOption)
			return bad("field type");
	}
	for (uint32_t i = 0; i < g.anchorCount; i++) {
		const Anchor& a = g.anchors[i];
		if (uint64_t(a.nameStart) + a.nameLength > g.stringCount || a.position > g.textCount)
			return bad("anchor range");
	}
	return true;
}

struct Want { uint8_t kind; uint8_t level; const char* text; };

static void Expect(const Doc& d, const char* what, const std::vector<Want>& want)
{
	bool ok = d.page.lineCount == want.size();
	for (uint32_t i = 0; ok && i < d.page.lineCount; i++) {
		const Line& l = d.page.lines[i];
		ok = l.kind == want[i].kind && l.level == want[i].level && d.LineText(i) == want[i].text;
	}
	std::string why;
	if (ok && !Valid(d, &why)) {
		printf("  invalid page: %s\n", why.c_str());
		ok = false;
	}
	Check(ok, what);
	if (!ok) {
		printf("  got %u lines:\n", d.page.lineCount);
		d.Dump();
		printf("  wanted %zu lines:\n", want.size());
		for (size_t i = 0; i < want.size(); i++)
			printf("  %3zu k%u L%u |%s|\n", i, want[i].kind, want[i].level, want[i].text);
	}
}

static void CheckEq(const std::string& got, const std::string& want, const char* what)
{
	gChecks++;
	if (got != want) {
		printf("FAIL %s\n  got    |%s|\n  wanted |%s|\n", what, got.c_str(), want.c_str());
		gFailures++;
	}
}

// ---- sample pages --------------------------------------------------------

static const char* kWiki = R"HTML(<!DOCTYPE html>
<html class="client-nojs" lang="pl" dir="ltr">
<head>
<meta charset="UTF-8">
<title>Charon (mitologia) – Wikipedia, wolna encyklopedia</title>
<script>document.documentElement.className="client-js";RLCONF={"wgPageName":"Charon_(mitologia)","x":"</p><h1>nie</h1>"};</script>
<link rel="stylesheet" href="/w/load.php?lang=pl&amp;modules=site.styles&amp;only=styles">
<style>.mw-body{margin:0} a>b{color:#00f}</style>
<base href="https://pl.wikipedia.org/wiki/">
</head>
<body class="skin-vector">
<a class="mw-jump-link" href="#bodyContent">Przejdź do zawartości</a>
<div id="mw-navigation"><nav><ul>
<li id="n-mainpage"><a href="/wiki/Wikipedia:Strona_g%C5%82%C3%B3wna" title="Strona główna [z]"><span>Strona główna</span></a></li>
<li><a href="/wiki/Specjalna:Losowa_strona"><span>Losuj artykuł</span></a></li>
<li><a href="/wiki/Kategoria:Kategorie G&#322;&oacute;wne"><span>Kategorie artykułów</span></a></li>
</ul></nav></div>
<form action="/w/index.php" id="searchform"><input type="search" name="search" placeholder="Przeszukaj Wikipedię" aria-label="Przeszukaj Wikipedię" autocapitalize="sentences" title="Przeszukaj Wikipedię [f]" accesskey="f"><input type="hidden" name="title" value="Specjalna:Szukaj"><button class="cdx-button">Szukaj</button></form>
<main id="content">
<h1 id="firstHeading"><span class="mw-page-title-main">Charon (mitologia)</span></h1>
<div id="bodyContent">
<table class="infobox"><tbody><tr><th colspan="2">Charon</th></tr>
<tr><td colspan="2"><a href="/wiki/Plik:Charon.jpg" class="mw-file-description"><img alt="Ilustracja" src="//upload.wikimedia.org/thumb/Charon.jpg" width="240"></a></td></tr>
<tr><th>Występowanie</th><td><a href="/wiki/Mitologia_grecka" title="Mitologia grecka">mitologia grecka</a></td></tr>
<tr><th>Siedziba</th><td><a href="/wiki/Hades_(kraina)">Hades</a></td></tr>
</tbody></table>
<p><b>Charon</b> (<a href="/wiki/J%C4%99zyk_grecki" title="Język grecki">gr.</a> <span lang="grc">Χάρων</span> <i>Chárōn</i>, <a href="/wiki/%C5%81acina">łac.</a> <i>Charon</i>)&#160;– w <a href="/wiki/Mitologia_grecka">mitologii greckiej</a> duch świata podziemnego, przewoźnik dusz zmarłych przez rzekę <a href="/wiki/Styks">Styks</a> lub <a href="/wiki/Acheron">Acheron</a><sup id="cite_ref-1" class="reference"><a href="#cite_note-1">&#91;1&#93;</a></sup>. Zapłatą dla Charona był <a href="/wiki/Obol">obol</a>, którego wkładano zmarłym do ust.</p>
<div id="toc" class="toc" role="navigation"><h2 id="mw-toc-heading">Spis treści</h2>
<ul><li class="toclevel-1"><a href="#Rodowód"><span class="tocnumber">1</span> <span class="toctext">Rodowód</span></a></li>
<li class="toclevel-1"><a href="#W_kulturze"><span class="tocnumber">2</span> <span class="toctext">W kulturze</span></a>
<ul><li class="toclevel-2"><a href="#Literatura"><span class="tocnumber">2.1</span> <span class="toctext">Literatura</span></a></li></ul></li>
<li class="toclevel-1"><a href="#Przypisy"><span class="tocnumber">3</span> <span class="toctext">Przypisy</span></a></li></ul></div>
<h2><span class="mw-headline" id="Rodowód">Rodowód</span><span class="mw-editsection">[<a href="/w/index.php?title=Charon_(mitologia)&amp;action=edit&amp;section=1" title="Edytuj sekcję: Rodowód">edytuj</a>]</span></h2>
<p>Był synem <a href="/wiki/Ereb">Ereba</a> i <a href="/wiki/Nyks">Nyks</a>. Przedstawiano go jako ponurego, brodatego starca w&nbsp;łachmanach.</p>
<figure><a href="/wiki/Plik:Lytovchenko.jpg"><img src="//upload.wikimedia.org/Lytovchenko.jpg" width="220" height="152"></a><figcaption>Aleksandr Litowczenko, <i>Charon przewozi dusze</i></figcaption></figure>
<h2><span class="mw-headline" id="W_kulturze">W kulturze</span></h2>
<h3><span class="mw-headline" id="Literatura">Literatura</span></h3>
<ul><li><a href="/wiki/Wergiliusz">Wergiliusz</a>, <i>Eneida</i>, księga VI</li>
<li><a href="/wiki/Dante_Alighieri">Dante</a>, <i>Boska komedia</i> &mdash; &bdquo;Piekło&rdquo;, pieśń III</li>
<li>Lukian z Samosat, <i>Rozmowy zmarłych</i></li></ul>
<blockquote><p>Przewoźnik strzeże tych wód i rzeki straszliwy, Charon, w ohydnym brudzie&hellip;</p></blockquote>
<h2><span class="mw-headline" id="Przypisy">Przypisy</span></h2>
<ol class="references"><li id="cite_note-1"><span class="mw-cite-backlink"><a href="#cite_ref-1">↑</a></span> <span class="reference-text">Pierre Grimal: <i>Słownik mitologii greckiej i rzymskiej</i>. Wrocław: Ossolineum, 2008, s. 59.</span></li></ol>
<hr>
<noscript><img src="//pl.wikipedia.org/wiki/Special:CentralAutoLogin/start?type=1x1" alt="" width="1" height="1"></noscript>
</div></main>
<footer><ul><li>Tę stronę ostatnio edytowano 3 lis 2026, 10:21.</li><li><a href="//foundation.wikimedia.org/wiki/Privacy_policy">Polityka prywatności</a></li></ul></footer>
<script>(RLQ=window.RLQ||[]).push(function(){mw.config.set({"wgBackendResponseTime":120});});</script>
</body></html>
)HTML";

static const char* kSearch = R"HTML(<html><head><meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<TITLE>charon uefi at  Szukacz</TITLE></head>
<BODY BGCOLOR=white>
<center><FORM ACTION="/html/" METHOD="GET" name=x>
<INPUT TYPE=text NAME=q VALUE="charon uefi" SIZE=40 maxlength=500>
<input type=hidden name=kl value="pl-pl"><input type="hidden" name="s" value="0">
<select name="df"><option value="">Dowolny czas</option><option value="d">Ostatnia doba<option value="w" selected>Ostatni tydzień</select>
<input type=checkbox name=safe value=1 checked> Filtr rodzinny
<INPUT TYPE=submit VALUE="Szukaj"></FORM></center>
<table border=0 width=100%>
<tr><td valign=top>1.&nbsp;</td><td><a rel="nofollow" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.org%2Fcharon&amp;rut=abc" class='result-link'>Charon – mówiący menedżer rozruchu UEFI</a></td></tr>
<tr><td>&nbsp;&nbsp;&nbsp;</td><td class='result-snippet'>Menedżer rozruchu z wbudowanym <b>czytnikiem ekranu</b>: czyta menu GRUB, rEFInd i Windows Boot Manager.</td></tr>
<tr><td>&nbsp;&nbsp;&nbsp;</td><td><span class='link-text'>example.org/charon</span></td></tr>
<tr><td>&nbsp;</td><td>&nbsp;</td></tr>
<tr><td valign=top>2.&nbsp;</td><td><a href=https://pl.wikipedia.org/wiki/Charon_(mitologia)>Charon (mitologia) – Wikipedia</a></td></tr>
<tr><td>&nbsp;&nbsp;&nbsp;</td><td class='result-snippet'>Charon &ndash; w mitologii greckiej przewoźnik dusz.</td></tr>
</table>
<form action="/html/" method="post"><input type="submit" value="Następna strona &gt;"><input type="hidden" name="q" value="charon uefi"><input type=hidden name=s value=30></form>
<p><a href="/feedback?q=charon uefi">Opinia</a> | <a href="../about">O nas</a>
</BODY></html>)HTML";

// ---- tests ---------------------------------------------------------------

static void TestBasics()
{
	Section("structure");
	Doc d;
	Check(d.Html("<!DOCTYPE html><html><head><title> Strona  g&#322;&oacute;wna\n</title>"
		"<style>p{color:red}</style><script>var a=\"<p>nie</p>\";</script></head>\n"
		"<body><h1>Witaj &amp; czytaj</h1><p>Pierwszy   akapit<br>druga\n\tlinia</p>"
		"<!-- komentarz <p>x</p> --><P CLASS=x>Trzeci &lt;akapit&gt;</P>"
		"<noscript>Bez skryptu</noscript><hr><h2>Drugi</h2>tekst<title>inny</title></body></html>"),
		"basic page renders completely");
	Expect(d, "headings, paragraphs, br, comments, script/style skipped, noscript kept", {
		{kindHeading, 1, "Witaj & czytaj"},
		{kindText, 0, "Pierwszy akapit"},
		{kindText, 0, "druga linia"},
		{kindText, 0, "Trzeci <akapit>"},
		{kindText, 0, "Bez skryptu"},
		{kindRule, 0, "----------"},
		{kindHeading, 2, "Drugi"},
		{kindText, 0, "tekst"},
	});
	CheckEq(d.Title(), "Strona główna", "title, first one wins, whitespace collapsed");
	Check(d.page.lineCount == 8 && (d.page.lines[1].flags & lineFirst)
		&& !(d.page.lines[2].flags & lineFirst), "lineFirst on block starts, not after <br>");

	d.Html("<ul><li>jeden<li>dwa<ol start=3><li>trzy</li><li>cztery</li></ol></li><li>pięć</ul>"
		"<ol><li value=10>a<li>b<li></li></ol><dl><dt>Termin<dd>Opis</dl>");
	Expect(d, "lists: nesting depth, ordered numbers, start, value, empty item", {
		{kindListItem, 1, "jeden"},
		{kindListItem, 1, "dwa"},
		{kindListItem, 2, "3. trzy"},
		{kindListItem, 2, "4. cztery"},
		{kindListItem, 1, "pięć"},
		{kindListItem, 1, "10. a"},
		{kindListItem, 1, "11. b"},
		{kindText, 0, "Termin"},
		{kindText, 0, "Opis"},
	});
	d.options.words.bullet = u"*";
	d.Html("<ul><li>a</li><li>b</li></ul>");
	Expect(d, "bullet option", {{kindListItem, 1, "* a"}, {kindListItem, 1, "* b"}});
	d.options.words.bullet = nullptr;

	d.Html("<pre>\n  int a;\n\ttab\n\n&lt;x&gt; <a href=u>l</a>\n</pre>"
		"<table><caption>Tabela</caption><tr><th>Miasto</th><th>Ludność</th></tr>"
		"<tr><td>Kraków</td><td></td><td><p>800 tys.</p><div>osób</div></td></tr><tr><td> </td></tr></table>"
		"<blockquote><p>Cytat pierwszy</p><blockquote>Zagnieżdżony</blockquote>dalej</blockquote>"
		"<img src=a.png><img alt=\"\" src=b.png><img alt=\"Wykres\" src=c.png><img title='Z tytułu'>");
	Expect(d, "pre, table rows, block quotes, images", {
		{kindPre, 0, "  int a;"},
		{kindPre, 0, "        tab"},
		{kindPre, 0, ""},
		{kindPre, 0, "<x> l[1]"},
		{kindText, 0, "Tabela"},
		{kindTableRow, 0, "Miasto | Ludność"},
		{kindTableRow, 0, "Kraków | | 800 tys. osób"},
		{kindQuote, 1, "Cytat pierwszy"},
		{kindQuote, 2, "Zagnieżdżony"},
		{kindQuote, 1, "dalej"},
		{kindImage, 0, "image"},
		{kindImage, 0, "Wykres"},
		{kindImage, 0, "Z tytułu"},
	});
	Check(d.page.lineCount > 9 && (d.page.lines[8].flags & lineInQuote)
		&& (d.page.lines[6].flags & lineInTable), "context flags");
	d.options.skipUnlabelledImages = true;
	d.Html("<img src=a.png><p>x");
	Expect(d, "skipUnlabelledImages", {{kindText, 0, "x"}});
	d.options.skipUnlabelledImages = false;
}

static void TestLinks()
{
	Section("links");
	Doc d;
	d.Html("<p>Zobacz <a href=\" /a b \">pierwszy  link</a> oraz <a href=x.html?a=1&amp;b=2&copy=3>drugi </a>.</p>"
		"<p><a href=\"#top\">Sam link</a></p><a href=\"e\"></a><a>bez href</a>"
		"<a href=\"img.html\"><img src=i.png alt=\"Logo firmy\"></a>"
		"<a href=\"card\"><div>Karta</div></a><a href=t title='Z tytułu'></a>"
		"<a href=i><img src=q.png></a>");
	Expect(d, "link markers, link-only lines, empty link dropped", {
		{kindText, 0, "Zobacz pierwszy link[1] oraz drugi[2] ."},
		{kindLink, 0, "Sam link[3]"},
		{kindText, 0, "bez href"},
		{kindImage, 0, "Logo firmy[4]"},
		{kindLink, 0, "Karta[5]"},
		{kindLink, 0, "Z tytułu[6]"},
		{kindImage, 0, "image[7]"},
	});
	Check(d.page.linkCount == 7, "link count");
	if (d.page.linkCount == 7) {
		CheckEq(d.LinkUrl(0), "/a b", "href trimmed");
		CheckEq(d.LinkUrl(1), "x.html?a=1&b=2&copy=3", "href: &amp; decoded, &copy without ; kept");
		CheckEq(d.LinkText(0), "pierwszy link", "link text range");
		CheckEq(d.LinkText(1), "drugi", "link text without trailing space");
		CheckEq(d.LinkText(3), "Logo firmy", "image link text");
		CheckEq(d.LinkText(4), "Karta", "block link text");
		Check(d.page.links[0].line == 0 && d.page.links[1].line == 0 && d.page.links[2].line == 1
			&& d.page.links[4].line == 4, "Link::line");
		Check(d.page.lines[0].firstLink == 0 && d.page.lines[0].linkCount == 2
			&& d.page.lines[1].firstLink == 2 && d.page.lines[2].linkCount == 0
			&& d.page.lines[2].firstLink == kNone, "Line::firstLink/linkCount");
		Check((d.page.lines[3].flags & lineLinkOnly) && !(d.page.lines[0].flags & lineLinkOnly),
			"lineLinkOnly");
	}
	d.options.marker = markerBefore;
	d.Html("a <a href=1>b c</a> d <a href=2>e</a>");
	Expect(d, "markerBefore", {{kindText, 0, "a [1]b c d [2]e"}});
	CheckEq(d.page.linkCount == 2 ? d.LinkText(0) : "", "b c", "markerBefore text range");
	d.Html("<a href=1>b c</a>");
	Expect(d, "markerBefore link-only", {{kindLink, 0, "[1]b c"}});
	d.options.marker = markerNone;
	d.Html("a <a href=1>b c</a> d");
	Expect(d, "markerNone", {{kindText, 0, "a b c d"}});
	d.options.marker = markerAfter;

	// a link wrapped over lines keeps one contiguous text range
	d.options.width = 20;
	d.Html("<p>Początek <a href=u>bardzo długi tekst odnośnika który się łamie</a> koniec");
	Expect(d, "wrapped link", {
		{kindText, 0, "Początek bardzo"},
		{kindLink, 0, "długi tekst"},
		{kindLink, 0, "odnośnika który się"},
		{kindText, 0, "łamie[1] koniec"},
	});
	CheckEq(d.page.linkCount ? d.LinkText(0) : "", "bardzo długi tekst odnośnika który się łamie",
		"wrapped link text is contiguous in the pool");
	Check(d.page.linkCount == 1 && d.page.links[0].line == 0
		&& (d.page.lines[1].flags & lineContinues) && d.page.lines[1].linkCount == 0,
		"wrapped link belongs to the line it starts on");
	// 100 links: three-digit markers
	std::string many;
	for (int i = 0; i < 120; i++)
		many += "<a href=" + std::to_string(i) + ">l</a> ";
	d.options.width = 78;
	d.Html(many);
	std::string why;
	Check(d.page.linkCount == 120 && Valid(d, &why), "120 links valid");
	CheckEq(d.LineText(d.page.lineCount - 1).substr(d.LineText(d.page.lineCount - 1).size() - 6),
		"l[120]", "three-digit marker");
}

static void TestWrap()
{
	Section("wrapping");
	Doc d;
	d.options.width = 20;
	d.Html("<p>Ala ma kota a kot ma Alę i bardzodługiewyrazyktóreniemieszcząsięwjednejlinii koniec 12345678901234567890 x</p>");
	Expect(d, "wrap at spaces, split long words", {
		{kindText, 0, "Ala ma kota a kot ma"},
		{kindText, 0, "Alę i"},
		{kindText, 0, "bardzodługiewyrazykt"},
		{kindText, 0, "óreniemieszcząsięwje"},
		{kindText, 0, "dnejlinii koniec"},
		{kindText, 0, "12345678901234567890"},
		{kindText, 0, "x"},
	});
	Check(d.page.lineCount == 7 && (d.page.lines[0].flags & lineFirst)
		&& !(d.page.lines[0].flags & lineContinues) && (d.page.lines[1].flags & lineContinues)
		&& !(d.page.lines[1].flags & lineFirst), "lineContinues on wrapped lines");
	// the pool is the plain text: one separator between lines
	CheckEq(Utf8(d.page.text, d.page.textCount),
		"Ala ma kota a kot ma Alę i bardzodługiewyrazyktóreniemieszcząsięwjednejlinii koniec "
		"12345678901234567890 x\n", "text pool is the readable plain text");
	d.Html("<ol start=99><li>aaaa bbbb cccc dddd eeee ffff</ol><h3>Nagłówek który jest długi</h3>"
		"<pre>0123456789012345678901234567890</pre>");
	Expect(d, "wrap list items, headings, pre", {
		{kindListItem, 1, "99. aaaa bbbb cccc"},
		{kindListItem, 1, "dddd eeee ffff"},
		{kindHeading, 3, "Nagłówek który jest"},
		{kindHeading, 3, "długi"},
		{kindPre, 0, "01234567890123456789"},
		{kindPre, 0, "01234567890"},
	});
	// a surrogate pair is never split
	d.options.width = 16;
	d.Html("<p>123456789012345&#x1F600;&#128512;x");
	Expect(d, "surrogate pair kept together at the wrap", {
		{kindText, 0, "123456789012345"},
		{kindText, 0, "\xF0\x9F\x98\x80\xF0\x9F\x98\x80x"},
	});
	d.options.width = 0;
	std::string longText = "<p>";
	for (int i = 0; i < 300; i++)
		longText += "słowo ";
	d.Html(longText);
	Check(d.page.lineCount == 1 && d.page.lines[0].length == 300 * 6 - 1, "width 0 never wraps");
	d.options.width = 3;  // below the minimum: 16 is used
	d.Html("<p>aaaa bbbb cccc dddd eeee");
	Expect(d, "minimum width", {{kindText, 0, "aaaa bbbb cccc"}, {kindText, 0, "dddd eeee"}});
}

static void TestForms()
{
	Section("forms");
	Doc d;
	d.Html("<form action=\"/search?old=1#f\" method=get><input type=hidden name=hl value=pl>"
		"<input type=\"text\" name=\"q\" value=\"kot &amp; pies\" placeholder=\"Szukaj\">"
		"<input type=checkbox name=safe checked><input type=checkbox name=adv value=1>"
		"<input type=radio name=t value=web checked><input type=radio name=t value=img>"
		"<select name=lang><option value=pl>Polski<option value=en selected> English  (UK) </select>"
		"<textarea name=note>\nlinia 1\r\nlinia &lt;2&gt;</textarea><input type=password name=pw value=tajne>"
		"<input name=off value=x disabled><input type=file name=plik><input value=bez_nazwy>"
		"<input type=submit name=btn value=\"Szukaj\"><input type=submit name=other value=Inne>"
		"<button name=b2 value=v2>Inny <b>przycisk</b></button><input type=reset><input type=image alt=Lupa name=im>"
		"</form><input name=poza value=1><form action=post.php method=POST><input name=a value=b></form>");
	Expect(d, "form control lines", {
		{kindForm, 0, "edit Szukaj: kot & pies"},
		{kindForm, 0, "check box safe checked"},
		{kindForm, 0, "check box adv: 1 not checked"},
		{kindForm, 0, "radio button t: web checked"},
		{kindForm, 0, "radio button t: img not checked"},
		{kindForm, 0, "list lang: English (UK)"},
		{kindForm, 0, "text area note: linia 1 linia <2>"},
		{kindForm, 0, "password pw"},
		{kindForm, 0, "edit off: x"},
		{kindForm, 0, "file plik"},
		{kindForm, 0, "edit: bez_nazwy"},
		{kindForm, 0, "button Szukaj"},
		{kindForm, 0, "button Inne"},
		{kindForm, 0, "button Inny przycisk"},
		{kindForm, 0, "button Reset"},
		{kindForm, 0, "button Lupa"},
		{kindForm, 0, "edit poza: 1"},
		{kindForm, 0, "edit a: b"},
	});
	const Page& g = d.page;
	Check(g.formCount == 2 && g.forms[0].method == 0 && g.forms[1].method == 1, "forms and methods");
	CheckEq(g.formCount ? d.String(g.forms[0].actionStart, g.forms[0].actionLength) : "",
		"/search?old=1#f", "form action");
	uint32_t q = kNone, btn = kNone, b2 = kNone, lang = kNone, safe = kNone, adv = kNone,
		note = kNone, poza = kNone;
	for (uint32_t i = 0; i < g.fieldCount; i++) {
		const std::string name = d.String(g.fields[i].nameStart, g.fields[i].nameLength);
		if (name == "q") q = i;
		if (name == "btn") btn = i;
		if (name == "b2") b2 = i;
		if (name == "lang") lang = i;
		if (name == "safe") safe = i;
		if (name == "adv") adv = i;
		if (name == "note") note = i;
		if (name == "poza") poza = i;
	}
	Check(q != kNone && btn != kNone && b2 != kNone && lang != kNone && safe != kNone
		&& adv != kNone && note != kNone && poza != kNone, "fields found by name");
	if (gFailures)
		return;
	Check(g.fields[q].type == fieldText && g.fields[q].form == 0 && g.fields[q].line == 0
		&& g.lines[0].field == q, "text field data");
	CheckEq(d.String(g.fields[q].valueStart, g.fields[q].valueLength), "kot & pies", "field value");
	CheckEq(d.String(g.fields[q].labelStart, g.fields[q].labelLength), "Szukaj", "field label");
	Check(g.fields[0].type == fieldHidden && g.fields[0].line == kNone, "hidden field has no line");
	Check(g.fields[lang].type == fieldSelect && g.fields[lang + 1].type == fieldOption
		&& g.fields[lang + 2].type == fieldOption && (g.fields[lang + 2].flags & fieldSelected),
		"select followed by its options");
	CheckEq(d.String(g.fields[lang].valueStart, g.fields[lang].valueLength), "en", "select value");
	CheckEq(d.String(g.fields[lang + 2].labelStart, g.fields[lang + 2].labelLength), "English (UK)",
		"option label");
	CheckEq(d.String(g.fields[note].valueStart, g.fields[note].valueLength), "linia 1\nlinia <2>",
		"textarea value");
	Check(g.fields[poza].form == kNoField && g.forms[0].firstField == 0
		&& g.forms[0].firstField + g.forms[0].fieldCount == poza, "form field run");
	Check(g.fields[b2].type == fieldSubmit && g.lines[g.fields[b2].line].field == b2, "button field");

	const char base[] = "http://example.com/dir/page.html?x=1";
	char url[512];
	size_t length = 0;
	Check(BuildFormGetUrl(g, 0, base, strlen(base), nullptr, btn, url, sizeof url, &length),
		"BuildFormGetUrl");
	CheckEq(url, "http://example.com/search?hl=pl&q=kot+%26+pies&safe=on&t=web&lang=en"
		"&note=linia+1%0D%0Alinia+%3C2%3E&pw=tajne&btn=Szukaj", "GET url with defaults");
	Check(length == strlen(url), "GET url length");
	// user edits: text, check boxes, select, another button
	std::vector<FieldValue> values(g.fieldCount, FieldValue{nullptr, 0});
	const char typed[] = "zażółć gęślą+jaźń/100%";
	values[q] = {typed, strlen(typed)};
	values[lang] = {"pl", 2};
	values[note] = {"", 0};
	d.fields[safe].flags &= uint8_t(~fieldChecked);
	d.fields[adv].flags |= fieldChecked;
	Check(BuildFormGetUrl(g, 0, base, strlen(base), values.data(), b2, url, sizeof url, &length),
		"BuildFormGetUrl with values");
	CheckEq(url, "http://example.com/search?hl=pl&q=za%C5%BC%C3%B3%C5%82%C4%87+g%C4%99%C5%9Bl%C4%85"
		"%2Bja%C5%BA%C5%84%2F100%25&adv=1&t=web&lang=pl&note=&pw=tajne&b2=v2", "GET url with values");
	Check(BuildFormGetUrl(g, 0, base, strlen(base), nullptr, kNone, url, sizeof url)
		&& !strstr(url, "btn=") && !strstr(url, "b2="), "no submit button named");
	Check(!BuildFormGetUrl(g, 1, base, strlen(base), nullptr, kNone, url, sizeof url),
		"POST form reports false");
	CheckEq(url, "http://example.com/dir/post.php?a=b", "POST form: URL still built");
	Check(!BuildFormGetUrl(g, 2, base, strlen(base), nullptr, kNone, url, sizeof url) && !url[0],
		"bad form index");
	Check(!BuildFormGetUrl(g, 0, base, strlen(base), nullptr, btn, url, 40) && !url[0],
		"BuildFormGetUrl overflow");
	for (size_t cap = 0; cap < 140; cap++) {
		std::vector<char> small(cap + 1, '#');
		const bool ok = BuildFormGetUrl(g, 0, base, strlen(base), nullptr, btn, small.data(), cap);
		if (small[cap] != '#' || (ok && strlen(small.data()) >= cap)) {
			Check(false, "BuildFormGetUrl wrote past its buffer");
			break;
		}
	}

	const Pair pairs[] = {{"q", 1, "a b&c=d", 7}, {"puste", 5, "", 0}, {"ż", 2, "~!*'()", 6}};
	Check(BuildGetUrl("http://h/p?old#frag", 19, pairs, 3, url, sizeof url, &length), "BuildGetUrl");
	CheckEq(url, "http://h/p?q=a+b%26c%3Dd&puste=&%C5%BC=%7E%21*%27%28%29", "BuildGetUrl encoding");
	Check(BuildGetUrl("http://h/p", 10, nullptr, 0, url, sizeof url) && !strcmp(url, "http://h/p"),
		"BuildGetUrl without pairs");
	Check(!BuildGetUrl("http://h/p", 10, pairs, 3, url, 12) && !url[0], "BuildGetUrl overflow");

	// select left open, nested controls, button with a block inside
	d.Html("<form><select name=s><option>jeden<option>dwa<input name=po><button>A<p>B</button>"
		"<select name=pusty></select><select name=z><optgroup label=G><option value=1>x</optgroup>");
	Expect(d, "unclosed select and button", {
		{kindForm, 0, "list s: jeden"},
		{kindForm, 0, "edit po"},
		{kindForm, 0, "button A"},
		{kindText, 0, "B"},
		{kindForm, 0, "list pusty"},
		{kindForm, 0, "list z: x"},
	});
}

static void TestBroken()
{
	Section("malformed input");
	Doc d;
	d.Html("<p>a<b>bold<p>next <a href=x>link <i>unclosed<p>para <!-- never closed");
	Expect(d, "unclosed tags and comment", {
		{kindText, 0, "abold"},
		{kindText, 0, "next link unclosed"},
		{kindLink, 0, "para[1]"},
	});
	d.Html("x < y & z > w &unknown; &#; &#x; &amp &ampx; AT&T <3 </ > <>");
	Expect(d, "stray < & >", {{kindText, 0, "x < y & z > w &unknown; &#; &#x; & &ampx; AT&T <3 </ > <>"}});
	d.Html("<A HREF='q\"r' TITLE=t>T</A><IMG ALT=Obrazek SRC=x><BR/><Br >po<hr/ >"
		"<p align = center >środek<a href = \"z z\"  >L</a ><a href=>pusty</a>");
	Expect(d, "upper case, odd attribute syntax", {
		{kindLink, 0, "T[1]"},
		{kindImage, 0, "Obrazek"},
		{kindText, 0, "po"},
		{kindRule, 0, "----------"},
		{kindText, 0, "środekL[2]pusty[3]"},
	});
	CheckEq(d.page.linkCount ? d.LinkUrl(0) : "", "q\"r", "quote inside the other quote");
	d.Html("<p>przed<input value=abc");
	Expect(d, "document ends inside a tag", {{kindText, 0, "przed"}});
	d.Html("<p>przed<a href=\"niezamknięty>tekst</a> dalej");
	Expect(d, "unterminated quote swallows the rest", {{kindText, 0, "przed"}});
	d.Html("a<![CDATA[ <b>surowe</b> &amp; ]]>b<?xml version=\"1.0\"?>c<!DOCTYPE x>d<!>e<!-->f<!--->g");
	Expect(d, "CDATA, processing instruction, doctype, empty comments",
		{{kindText, 0, "a <b>surowe</b> &amp; bcdefg"}});
	d.Html("<script>a</script >b<SCRIPT>c</SCRIPT\n>d<style>e</stylex>f</style>g<script>nigdy");
	Expect(d, "script/style end tags", {{kindText, 0, "bdg"}});
	d.Html("<svg><title>ikona</title><text>x</text></svg>a<svg/>b<iframe src=x>c</iframe>d"
		"<template><p>t</p></template>e<textarea>w <b>środku</b></textarea");
	Expect(d, "svg, iframe, template skipped; textarea raw", {
		{kindText, 0, "abde"},
		{kindForm, 0, "text area: w <b>środku</b>"},
	});
	CheckEq(d.Title(), "", "svg title is not the page title");
	d.Html("</p></div></ul></table></a></li></tr></td></form></select></button></h1></pre>"
		"</blockquote>tekst<td>komórka<tr><li>punkt</ul>");
	Expect(d, "end tags without start tags, cells without a table", {
		{kindText, 0, "tekst"},
		{kindText, 0, "komórka"},
		{kindText, 0, "punkt"},
	});
	// deep nesting beyond kMaxDepth
	std::string deep;
	for (int i = 0; i < 100; i++)
		deep += "<ul><li>p" + std::to_string(i);
	for (int i = 0; i < 100; i++)
		deep += "<table><tr><td><blockquote>";
	deep += "dno";
	d.Html(deep);
	std::string why;
	Check(d.complete && d.page.lineCount == 101 && Valid(d, &why), "nesting deeper than kMaxDepth");
	Check(d.page.lineCount == 101 && d.page.lines[40].level == kMaxDepth, "depth is capped");
	// an attribute longer than kMaxAttribute: the link is not usable, text stays
	std::string longAttr = "<a href=\"" + std::string(5000, 'u') + "\">t</a><img alt=\""
		+ std::string(5000, 'a') + "\"><p>po";
	d.Html(longAttr);
	Check(d.complete && d.page.linkCount == 0 && d.page.lineCount > 20 && d.LineText(0) == "t"
		&& d.page.lines[1].kind == kindImage && d.page.lines[1].length == 78
		&& d.page.lines[d.page.lineCount - 2].kind == kindImage
		&& d.LineText(d.page.lineCount - 1) == "po" && d.page.textCount < kMaxAttribute + 100
		&& Valid(d, &why), "over-long href is dropped, over-long alt is cut");
	Check(d.page.stringCount == 0, "unused attributes leave nothing in the string pool");
	d.Html("");
	Check(d.complete && d.page.lineCount == 0 && d.page.textCount == 0, "empty document");
	Check(Render(nullptr, 100, Charset::Auto, d.page) && d.page.lineCount == 0, "null input");
	d.Html(std::string("a\0b<p\0>c", 9));
	Check(d.complete && Valid(d), "NUL bytes");
	// table inside list inside table, rows cut by blocks
	d.Html("<table><tr><td>a<ul><li>b<table><tr><td>c<td>d</table>e</ul>f<td>g</table>h");
	Expect(d, "nested tables and lists", {
		{kindTableRow, 0, "a"},
		{kindListItem, 1, "b"},
		{kindTableRow, 0, "c | d"},
		{kindListItem, 1, "e"},
		{kindTableRow, 0, "f | g"},
		{kindText, 0, "h"},
	});
}

static void TestEntities()
{
	Section("entities");
	Doc d;
	d.Html("&nbsp;&amp;&lt;&gt;&quot;&apos;|&oacute;&Oacute;&aogon;&Aogon;&cacute;&eogon;&lstrok;&Lstrok;"
		"&nacute;&sacute;&zacute;&zdot;&Zdot;|&auml;&ouml;&uuml;&szlig;&eacute;&egrave;&ccedil;&ntilde;"
		"&aring;&oslash;&aelig;&Agrave;&yuml;|&ldquo;&rdquo;&bdquo;&lsquo;&rsquo;&sbquo;&laquo;&raquo;"
		"&ndash;&mdash;&hellip;&copy;&reg;&trade;&euro;&pound;&sect;&deg;&plusmn;&times;&divide;"
		"&bull;&middot;&rarr;&larr;&alpha;&Omega;&permil;&dagger;&frac12;&scaron;&Scaron;&hearts;");
	Expect(d, "named entities", {{kindText, 0,
		"&<>\"'|óÓąĄćęłŁńśźżŻ|äöüßéèçñåøæÀÿ|“”„‘’‚«»–—…©®™€£§°±×÷•·→←αΩ‰†½šŠ♥"}});
	d.Html("&#65;&#x42;&#X43;&#322;&#x142;&#128512;&#x1F600;&#0;&#xD800;&#x110000;&#99999999999;"
		"&#150;&#x93;&#65&#x42 &#3;.");
	Expect(d, "numeric entities", {{kindText, 0,
		"ABCłł\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD–“AB ."}});
	Check(d.page.textCount > 9 && d.page.text[5] == 0xD83D && d.page.text[6] == 0xDE00,
		"surrogate pair for U+1F600");
	d.Html("&copy 2026 &reg&nbsp&eacute; &euro 5 &rarr x &notanentity; &thetasym; &amp;amp;");
	Expect(d, "entities without semicolon", {{kindText, 0, "© 2026 ® é &euro 5 &rarr x &notanentity; ϑ &amp;"}});
	d.Html("<a href=\"?a=1&amp;b=2&lt=3&copy=4&#38;c=&oacute;&copy\">x</a><img alt='A &amp; B &lt;&#322;&gt;'>");
	CheckEq(d.page.linkCount ? d.LinkUrl(0) : "", "?a=1&b=2&lt=3&copy=4&c=ó&copy", "entities in attributes");
	CheckEq(d.LineText(1), "A & B <ł>", "entities in alt");
	d.Html("a&shy;b&#8203;c&zwnj;d&#xFEFF;e \xC2\xA0 &nbsp; f");
	Expect(d, "soft hyphen and zero-width characters dropped, nbsp is a space",
		{{kindText, 0, "abcde f"}});
	// every table entry decodes to its code point
	bool all = true;
	for (const auto& e : detail::kEntities) {
		const std::string html = std::string("<p>x&") + e.name + ";y";
		std::vector<uint8_t> bytes(html.begin(), html.end());
		size_t p = 4;
		const uint32_t cp = detail::Reference(bytes.data(), bytes.size(), p, true);
		if (cp != e.code || p != html.size() - 1)
			all = false;
	}
	Check(all && sizeof detail::kEntities / sizeof detail::kEntities[0] > 252,
		"all table entities decode (HTML 4 set and more)");
}

static void TestCharsets()
{
	Section("charsets");
	Doc d;
	// "Zażółć gęślą jaźń" in the three encodings
	const std::string latin2 = "Za\xBF\xF3\xB3\xE6 g\xEA\xB6l\xB1 ja\xBC\xF1 \xA6\xAC";
	const std::string cp1250 = "Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1 \x8C\x8F \x84x\x94 \x96 \x85";
	const std::string cp1252 = "caf\xE9 na\xEFve \x93q\x94 \x80 \x96 \x8A";
	d.Html("<p>" + latin2, Charset::Latin2);
	Expect(d, "ISO-8859-2 by hint", {{kindText, 0, "Zażółć gęślą jaźń ŚŹ"}});
	d.Html("<p>" + cp1250, Charset::Windows1250);
	Expect(d, "windows-1250 by hint", {{kindText, 0, "Zażółć gęślą jaźń ŚŹ „x” – …"}});
	d.Html("<p>" + cp1252, Charset::Windows1252);
	Expect(d, "windows-1252 by hint", {{kindText, 0, "café naïve “q” € – Š"}});
	d.Html("<p>caf\xE9 \xA9", Charset::Latin1);
	Expect(d, "ISO-8859-1 by hint", {{kindText, 0, "café ©"}});
	d.Html("<html><head><META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=iso-8859-2\">"
		"<title>\xA3\xF3\x64\xBC</title></head><p>" + latin2);
	Expect(d, "meta http-equiv charset", {{kindText, 0, "Zażółć gęślą jaźń ŚŹ"}});
	CheckEq(d.Title(), "Łódź", "title in the declared charset");
	Check(d.page.charset == Charset::Latin2, "Page::charset reports the charset used");
	d.Html("<meta charset=windows-1250><p>" + cp1250);
	Expect(d, "meta charset unquoted", {{kindText, 0, "Zażółć gęślą jaźń ŚŹ „x” – …"}});
	d.Html("<meta name=x content=y><meta charset='Windows-1252'/><p>" + cp1252);
	Expect(d, "meta charset quoted, second meta", {{kindText, 0, "café naïve “q” € – Š"}});
	d.Html("<meta charset=iso-8859-2><p>" + cp1250, Charset::Windows1250);
	Expect(d, "the hint (HTTP header) beats <meta>", {{kindText, 0, "Zażółć gęślą jaźń ŚŹ „x” – …"}});
	d.Html("\xEF\xBB\xBF<meta charset=iso-8859-2><p>Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", Charset::Latin2);
	Expect(d, "the UTF-8 byte order mark beats everything", {{kindText, 0, "Zażółć"}});
	d.Html("<p>" + cp1252);
	Expect(d, "undeclared, not UTF-8: fallback windows-1252", {{kindText, 0, "café naïve “q” € – Š"}});
	d.options.fallback = Charset::Windows1250;
	d.Html("<p>" + cp1250);
	Expect(d, "Options::fallback", {{kindText, 0, "Zażółć gęślą jaźń ŚŹ „x” – …"}});
	d.options.fallback = Charset::Windows1252;
	d.Html("<p>Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 \xF0\x9F\x98\x80");
	Expect(d, "undeclared valid UTF-8", {{kindText, 0, "Zażółć \xF0\x9F\x98\x80"}});
	Check(d.page.charset == Charset::Utf8, "UTF-8 detected");
	d.Html("<p>a\xC5z \xE2\x82 \xC0\x80 \xED\xA0\x80 \xF5 \xF0\x9F\x98", Charset::Utf8);
	Expect(d, "malformed UTF-8 becomes U+FFFD", {{kindText, 0,
		"a\xEF\xBF\xBDz \xEF\xBF\xBD\xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD "
		"\xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"}});
	Check(CharsetFromName("ISO-8859-2", 10) == Charset::Latin2
		&& CharsetFromName("utf-8", 5) == Charset::Utf8 && CharsetFromName("UTF8", 4) == Charset::Utf8
		&& CharsetFromName("windows-1250", 12) == Charset::Windows1250
		&& CharsetFromName("CP1252", 6) == Charset::Windows1252
		&& CharsetFromName("iso-8859-1", 10) == Charset::Windows1252
		&& CharsetFromName("koi8-r", 6) == Charset::Auto
		&& CharsetFromName(nullptr, 3) == Charset::Auto, "CharsetFromName");
#ifdef _WIN32
	// The three tables against the system's own conversion, all 256 bytes.
	const struct { Charset charset; UINT page; const char* name; } tables[] = {
		{Charset::Windows1250, 1250, "windows-1250 table matches Windows"},
		{Charset::Windows1252, 1252, "windows-1252 table matches Windows"},
		{Charset::Latin2, 28592, "ISO-8859-2 table matches Windows"},
		{Charset::Latin1, 28591, "ISO-8859-1 table matches Windows"},
	};
	for (const auto& t : tables) {
		bool same = true;
		for (int b = 0; b < 256; b++) {
			const uint8_t byte = uint8_t(b);
			wchar_t wide[4] = {};
			const int n = MultiByteToWideChar(t.page, 0, reinterpret_cast<const char*>(&byte), 1, wide, 4);
			size_t p = 0;
			const uint32_t cp = detail::DecodeByte(t.charset, &byte, 1, p);
			if (n != 1 || cp != uint32_t(wide[0]) || p != 1) {
				printf("  byte %02X: %04X, Windows %04X\n", b, cp, unsigned(wide[0]));
				same = false;
			}
		}
		Check(same, t.name);
	}
#endif
}

static std::string Resolve(const char* base, const char* reference)
{
	char out[256], fragment[64];
	size_t length = 0, fragmentLength = 0;
	if (!ResolveUrl(base, strlen(base), reference, strlen(reference), out, sizeof out, &length,
			fragment, sizeof fragment, &fragmentLength))
		return "<failed>";
	if (length != strlen(out) || fragmentLength != strlen(fragment))
		return "<bad length>";
	std::string result = out;
	if (strchr(reference, '#'))
		result += "#" + std::string(fragment);
	return result;
}

static void TestUrls()
{
	Section("URL resolution");
	const char* base = "http://a/b/c/d;p?q";
	// RFC 3986 section 5.4.1, normal examples
	static const char* normal[][2] = {
		{"g:h", "g:h"}, {"g", "http://a/b/c/g"}, {"./g", "http://a/b/c/g"},
		{"g/", "http://a/b/c/g/"}, {"/g", "http://a/g"}, {"//g", "http://g/"},
		{"?y", "http://a/b/c/d;p?y"}, {"g?y", "http://a/b/c/g?y"},
		{"#s", "http://a/b/c/d;p?q#s"}, {"g#s", "http://a/b/c/g#s"},
		{"g?y#s", "http://a/b/c/g?y#s"}, {";x", "http://a/b/c/;x"},
		{"g;x", "http://a/b/c/g;x"}, {"g;x?y#s", "http://a/b/c/g;x?y#s"},
		{"", "http://a/b/c/d;p?q"}, {".", "http://a/b/c/"}, {"./", "http://a/b/c/"},
		{"..", "http://a/b/"}, {"../", "http://a/b/"}, {"../g", "http://a/b/g"},
		{"../..", "http://a/"}, {"../../", "http://a/"}, {"../../g", "http://a/g"},
	};
	// 5.4.2, abnormal examples (strict parser)
	static const char* abnormal[][2] = {
		{"../../../g", "http://a/g"}, {"../../../../g", "http://a/g"},
		{"/./g", "http://a/g"}, {"/../g", "http://a/g"}, {"g.", "http://a/b/c/g."},
		{".g", "http://a/b/c/.g"}, {"g..", "http://a/b/c/g.."}, {"..g", "http://a/b/c/..g"},
		{"./../g", "http://a/b/g"}, {"./g/.", "http://a/b/c/g/"}, {"g/./h", "http://a/b/c/g/h"},
		{"g/../h", "http://a/b/c/h"}, {"g;x=1/./y", "http://a/b/c/g;x=1/y"},
		{"g;x=1/../y", "http://a/b/c/y"}, {"g?y/./x", "http://a/b/c/g?y/./x"},
		{"g?y/../x", "http://a/b/c/g?y/../x"}, {"g#s/./x", "http://a/b/c/g#s/./x"},
		{"g#s/../x", "http://a/b/c/g#s/../x"}, {"http:g", "http:g"},
	};
	// (the RFC writes "//g" -> "http://g"; an http URL without a path gets "/")
	for (const auto& e : normal)
		CheckEq(Resolve(base, e[0]), e[1], (std::string("RFC 5.4.1 ") + e[0]).c_str());
	for (const auto& e : abnormal)
		CheckEq(Resolve(base, e[0]), e[1], (std::string("RFC 5.4.2 ") + e[0]).c_str());
	CheckEq(Resolve("https://pl.wikipedia.org/wiki/Charon", "/wiki/Zażółć gęślą?x=a b#Sekcja ó"),
		"https://pl.wikipedia.org/wiki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85?x=a%20b#Sekcja%20%C3%B3",
		"spaces and non-ASCII are percent-encoded");
	CheckEq(Resolve("https://example.com", "x.html"), "https://example.com/x.html", "base without a path");
	CheckEq(Resolve("https://example.com", ""), "https://example.com/", "empty path gets /");
	CheckEq(Resolve("https://example.com/a/b?q#f", "//cdn.example.net/x/../y.css"),
		"https://cdn.example.net/y.css", "scheme-relative");
	CheckEq(Resolve("http://h/a/b", "HTTPS://Example.COM/A/./B/../C?Q#F"), "https://Example.COM/A/C?Q#F",
		"absolute reference, scheme lower-cased, dots removed");
	CheckEq(Resolve("http://h/a/b", "  \tc d\r\n.html \n"), "http://h/a/c%20d.html",
		"leading/trailing space trimmed, tabs and newlines dropped");
	CheckEq(Resolve("http://h/a/b", "mailto:jan@example.com"), "mailto:jan@example.com", "mailto kept");
	CheckEq(Resolve("http://h/a/b", "javascript:void(0)"), "javascript:void(0)", "javascript kept");
	CheckEq(Resolve("http://h:8080/a/b/", "../x%20y?z=%C5%82"), "http://h:8080/a/x%20y?z=%C5%82",
		"existing escapes are not encoded again");
	CheckEq(Resolve("http://user@h/a", "?"), "http://user@h/a?", "empty query");
	CheckEq(Resolve("http://h/a?b#c", "#"), "http://h/a?b#", "empty fragment");
	CheckEq(Resolve("relative/base", "x"), "<failed>", "relative base fails");
	CheckEq(Resolve("", "x"), "<failed>", "empty base fails");
	CheckEq(Resolve("", "http://h/x"), "http://h/x", "absolute reference without a base");
	CheckEq(Resolve("http://h/a/b/c", "../../../../.."), "http://h/", "too many ..");
	CheckEq(Resolve("http://h/%7Euser/a b/", "c\"<>`"), "http://h/%7Euser/a%20b/c%22%3C%3E%60",
		"base encoded too, unsafe ASCII encoded");
	Check(IsHttpUrl("http://a/", 9) && IsHttpUrl("HTTPS://a", 9) && !IsHttpUrl("mailto:a@b", 10)
		&& !IsHttpUrl("ftp://a/", 8) && !IsHttpUrl("/a", 2) && !IsHttpUrl("http:///x", 9)
		&& !IsHttpUrl(nullptr, 4), "IsHttpUrl");
	// overflow: every capacity below the needed one fails and stays in bounds
	const char* ref = "../x y/ż?q=1#frag";
	const std::string full = "http://a/b/x%20y/%C5%BC?q=1";
	bool ok = true;
	for (size_t cap = 0; cap <= full.size() + 2; cap++) {
		std::vector<char> out(cap + 1, '#');
		std::vector<char> frag(8, '#');
		size_t n = 99;
		const bool r = ResolveUrl(base, strlen(base), ref, strlen(ref), out.data(), cap, &n,
			frag.data(), 5, nullptr);
		if (out[cap] != '#' || frag[5] != '#')
			ok = false;
		// (room is needed for the path before "../" is removed: 5 more here)
		if ((r && cap <= full.size()) || (!r && cap > full.size() + 5))
			ok = false;
		if (r && (full != out.data() || n != full.size() || strcmp(frag.data(), "frag")))
			ok = false;
		if (!r && ((cap && out[0]) || n != 0))
			ok = false;
	}
	Check(ok, "ResolveUrl output capacity");
	char out[64], frag[4];
	Check(!ResolveUrl(base, strlen(base), "#toolong", 8, out, sizeof out, nullptr, frag, sizeof frag)
		&& !out[0] && !frag[0], "ResolveUrl fragment capacity");
	Check(ResolveUrl(base, strlen(base), "#toolong", 8, out, sizeof out)
		&& !strcmp(out, "http://a/b/c/d;p?q"), "fragment is optional");

	Doc d;
	d.Html("<base href='/dir/sub/'><a href='../x.html#sekcja'>a</a><a href='https://inny.example/'>b</a>"
		"<a href='?strona=2'>c</a>");
	char baseUrl[128];
	size_t baseLength = 0;
	const char* doc = "https://example.com/a/b/c.html?x=1";
	Check(PageBaseUrl(d.page, doc, strlen(doc), baseUrl, sizeof baseUrl, &baseLength), "PageBaseUrl");
	CheckEq(baseUrl, "https://example.com/dir/sub/", "base href resolved against the document");
	size_t fl = 0;
	char fr[32];
	Check(d.page.linkCount == 3 && ResolveLink(d.page, 0, baseUrl, baseLength, out, sizeof out,
		nullptr, fr, sizeof fr, &fl), "ResolveLink");
	CheckEq(out, "https://example.com/dir/x.html", "link resolved against <base>");
	CheckEq(fr, "sekcja", "link fragment");
	Check(ResolveLink(d.page, 2, baseUrl, baseLength, out, sizeof out)
		&& !strcmp(out, "https://example.com/dir/sub/?strona=2"), "query-only link");
	Check(!ResolveLink(d.page, 3, baseUrl, baseLength, out, sizeof out) && !out[0], "bad link index");
	d.Html("<a href=x>a</a>");
	Check(PageBaseUrl(d.page, doc, strlen(doc), baseUrl, sizeof baseUrl, &baseLength)
		&& !strcmp(baseUrl, doc), "PageBaseUrl without <base>");
}

static std::u16string U16(const char* ascii)
{
	std::u16string s;
	for (; *ascii; ascii++)
		s += char16_t(uint8_t(*ascii));
	return s;
}

static void TestFind()
{
	Section("find helpers");
	Doc d;
	d.options.width = 30;
	d.Html("<h1 id=top>Tytuł strony</h1><p>Wstęp z <a href=a>linkiem</a>.</p>"
		"<h2 id='sekcja 1'>Pierwsza sekcja o bardzo długim tytule</h2><p>Tekst bez odnośników, ale za to "
		"długi i zawijany na kilka linii ŻÓŁĆ.</p><h3>Podsekcja</h3><a name=kotwica></a><p>Koniec "
		"<span id='zażółć'>z</span> <a href=b>drugim</a>.</p><form><input name=q></form><h2>Ostatnia</h2>");
	const Page& g = d.page;
	Expect(d, "page for the find helpers", {
		{kindHeading, 1, "Tytuł strony"},
		{kindText, 0, "Wstęp z linkiem[1]."},
		{kindHeading, 2, "Pierwsza sekcja o bardzo"},
		{kindHeading, 2, "długim tytule"},
		{kindText, 0, "Tekst bez odnośników, ale za"},
		{kindText, 0, "to długi i zawijany na kilka"},
		{kindText, 0, "linii ŻÓŁĆ."},
		{kindHeading, 3, "Podsekcja"},
		{kindText, 0, "Koniec z drugim[2]."},
		{kindForm, 0, "edit q"},
		{kindHeading, 2, "Ostatnia"},
	});
	if (g.lineCount != 11)
		return;
	Check(FindNextHeading(g, 0, 1) == 0 && FindNextHeading(g, 1, 1) == 2
		&& FindNextHeading(g, 3, 1) == 7 && FindNextHeading(g, 8, 1) == 10
		&& FindNextHeading(g, 11, 1) == kNone, "FindNextHeading down (skips wrapped heading lines)");
	Check(FindNextHeading(g, 10, -1) == 10 && FindNextHeading(g, 9, -1) == 7
		&& FindNextHeading(g, 6, -1) == 2 && FindNextHeading(g, 1, -1) == 0
		&& FindNextHeading(g, 100, -1) == kNone, "FindNextHeading up");
	Check(FindNextHeading(g, 0, 1, 2) == 2 && FindNextHeading(g, 3, 1, 2) == 10
		&& FindNextHeading(g, 0, 1, 3) == 7 && FindNextHeading(g, 0, 1, 4) == kNone
		&& FindNextHeading(g, 10, -1, 1) == 0, "FindNextHeading by level");
	Check(FindNextLink(g, 0, 1) == 1 && FindNextLink(g, 2, 1) == 8 && FindNextLink(g, 9, 1) == kNone
		&& FindNextLink(g, 7, -1) == 1 && FindNextLink(g, 0, -1) == kNone, "FindNextLink");
	Check(FindNextField(g, 0, 1) == 9 && FindNextField(g, 10, -1) == 9
		&& FindNextKind(g, 0, 1, kindText) == 1 && FindNextKind(g, 2, 1, kindText) == 4
		&& FindNextKind(g, 0, 1, kindRule) == kNone, "FindNextField / FindNextKind");
	uint32_t column = 99;
	const std::u16string zolc = u"żółć", sekcja = u"SEKCJA", phrase = u"ale za to długi i zawijany",
		none = u"nie ma", tytul = u"tytuł";
	Check(FindText(g, zolc.data(), zolc.size(), 0, 1, &column) == 6 && column == 6,
		"FindText ignores case of Polish letters");
	Check(FindText(g, sekcja.data(), sekcja.size(), 0, 1, &column) == 2 && column == 9
		&& FindText(g, sekcja.data(), sekcja.size(), 3, 1, &column) == 7 && column == 3
		&& FindText(g, sekcja.data(), sekcja.size(), 8, 1) == kNone, "FindText down, ASCII case");
	Check(FindText(g, sekcja.data(), sekcja.size(), 10, -1) == 7
		&& FindText(g, sekcja.data(), sekcja.size(), 6, -1, &column) == 2 && column == 9
		&& FindText(g, sekcja.data(), sekcja.size(), 1, -1) == kNone, "FindText up");
	Check(FindText(g, phrase.data(), phrase.size(), 0, 1, &column) == 4 && column == 22,
		"FindText matches a phrase across wrapped lines");
	Check(FindText(g, none.data(), none.size(), 0, 1) == kNone
		&& FindText(g, none.data(), 0, 0, 1) == kNone && FindText(g, nullptr, 3, 0, 1) == kNone
		&& FindText(g, tytul.data(), tytul.size(), 50, 1) == kNone, "FindText misses");
	Check(FindText(g, tytul.data(), tytul.size(), 0, 1, &column) == 0 && column == 0
		&& FindText(g, tytul.data(), tytul.size(), 1, 1) == kNone
		&& FindText(g, u"TYTULE", 6, 1, 1, &column) == 3 && column == 7
		&& FindText(g, u"tytule", 6, 3, -1) == 3 && FindText(g, u"tytule", 6, 2, -1) == kNone
		&& FindText(g, tytul.data(), tytul.size(), 0, -1) == 0, "FindText from a line, both ways");
	const std::u16string greek = u"ΧΆΡΩΝ χάρων Привет";
	Check(detail::Fold(u'Ą') == u'ą' && detail::Fold(u'Ź') == u'ź' && detail::Fold(u'Ł') == u'ł'
		&& detail::Fold(u'Ż') == u'ż' && detail::Fold(u'Ń') == u'ń' && detail::Fold(u'Ś') == u'ś'
		&& detail::Fold(u'Ć') == u'ć' && detail::Fold(u'Ę') == u'ę' && detail::Fold(u'Ó') == u'ó'
		&& detail::Fold(u'ß') == u'ß' && detail::Fold(u'×') == u'×' && detail::Fold(u'Ü') == u'ü'
		&& detail::Fold(u'Š') == u'š' && detail::Fold(u'Ž') == u'ž' && detail::Fold(u'Ő') == u'ő'
		&& detail::Fold(u'Π') == u'π' && detail::Fold(u'Я') == u'я' && detail::Fold(u'ą') == u'ą'
		&& detail::Fold(u'z') == u'z' && detail::Fold(u'Ÿ') == u'ÿ', "case folding table");
	// anchors
	Check(g.anchorCount == 4, "anchors recorded");
	Check(FindAnchor(g, "top", 3) == 0 && FindAnchor(g, "sekcja 1", 8) == 2
		&& FindAnchor(g, "sekcja%201", 10) == 2 && FindAnchor(g, "kotwica", 7) == 8
		&& FindAnchor(g, "za%C5%BC%C3%B3%C5%82%C4%87", 26) == 8
		&& FindAnchor(g, "zażółć", strlen("zażółć")) == 8
		&& FindAnchor(g, "brak", 4) == kNone && FindAnchor(g, "to", 2) == kNone
		&& FindAnchor(g, "", 0) == kNone, "FindAnchor");
	Check(LineAt(g, 0) == 0 && LineAt(g, g.lines[3].start) == 3
		&& LineAt(g, g.lines[3].start + g.lines[3].length) == 4
		&& LineAt(g, g.textCount + 5) == g.lineCount - 1, "LineAt");
	Doc empty;
	empty.Html("");
	Check(LineAt(empty.page, 0) == kNone && FindNextHeading(empty.page, 0, 1) == kNone
		&& FindText(empty.page, zolc.data(), zolc.size(), 0, 1) == kNone
		&& FindNextLink(empty.page, 0, -1) == kNone, "helpers on an empty page");
	(void)U16;
	(void)greek;
}

static void TestPlain()
{
	Section("text/plain");
	Doc d;
	d.options.width = 24;
	Check(d.Plain("Pierwsza linia\r\nDruga  linia, <b>bez</b> &amp; znaczników\n\n\n   Trzeci akapit\tz tabulatorem\n"
		"http://example.com/bardzo/dlugi/adres/ktory/sie/nie/miesci\n"), "RenderPlain");
	Expect(d, "plain text lines", {
		{kindText, 0, "Pierwsza linia"},
		{kindText, 0, "Druga linia, <b>bez</b>"},
		{kindText, 0, "&amp; znaczników"},
		{kindText, 0, "Trzeci akapit z"},
		{kindText, 0, "tabulatorem"},
		{kindText, 0, "http://example.com/bardz"},
		{kindText, 0, "o/dlugi/adres/ktory/sie/"},
		{kindText, 0, "nie/miesci"},
	});
	Check(d.page.lineCount == 8 && (d.page.lines[0].flags & lineFirst)
		&& !(d.page.lines[1].flags & lineFirst) && (d.page.lines[3].flags & lineFirst)
		&& (d.page.lines[2].flags & lineContinues), "plain text: empty lines mark paragraphs");
	Check(d.page.linkCount == 0 && d.page.titleLength == 0, "plain text has no links or title");
	d.Plain("Za\xBF\xF3\xB3\xE6", Charset::Latin2);
	Expect(d, "plain text charset hint", {{kindText, 0, "Zażółć"}});
	d.Plain("\xEF\xBB\xBFZa\xC5\xBC");
	Expect(d, "plain text BOM", {{kindText, 0, "Zaż"}});
	d.Plain("<meta charset=iso-8859-2>\xA3");
	Expect(d, "plain text does not sniff <meta>", {{kindText, 0, "<meta"}, {kindText, 0, "charset=iso-8859-2>£"}});
	Doc tiny(10, 4, 0, 0, 0, 0, 0);
	Check(!tiny.Plain("0123456789abcdef") && tiny.page.truncated && Valid(tiny), "plain text overflow");
}

static void TestRealistic()
{
	Section("realistic pages");
	Doc d;
	Check(d.Html(kWiki), "Wikipedia-like article renders");
	std::string why;
	Check(Valid(d, &why), "Wikipedia-like article is valid");
	const Page& g = d.page;
	CheckEq(d.Title(), "Charon (mitologia) – Wikipedia, wolna encyklopedia", "article title");
	CheckEq(d.String(g.baseStart, g.baseLength), "https://pl.wikipedia.org/wiki/", "article base");
	Expect(d, "Wikipedia-like article", {
		{kindLink, 0, "Przejdź do zawartości[1]"},
		{kindListItem, 1, "Strona główna[2]"},
		{kindListItem, 1, "Losuj artykuł[3]"},
		{kindListItem, 1, "Kategorie artykułów[4]"},
		{kindForm, 0, "edit Przeszukaj Wikipedię"},
		{kindForm, 0, "button Szukaj"},
		{kindHeading, 1, "Charon (mitologia)"},
		{kindTableRow, 0, "Charon"},
		{kindTableRow, 0, "Ilustracja[5]"},
		{kindTableRow, 0, "Występowanie | mitologia grecka[6]"},
		{kindTableRow, 0, "Siedziba | Hades[7]"},
		{kindText, 0, "Charon (gr.[8] Χάρων Chárōn, łac.[9] Charon) – w mitologii greckiej[10] duch"},
		{kindText, 0, "świata podziemnego, przewoźnik dusz zmarłych przez rzekę Styks[11] lub"},
		{kindText, 0, "Acheron[12][1][13]. Zapłatą dla Charona był obol[14], którego wkładano zmarłym"},
		{kindText, 0, "do ust."},
		{kindHeading, 2, "Spis treści"},
		{kindListItem, 1, "1 Rodowód[15]"},
		{kindListItem, 1, "2 W kulturze[16]"},
		{kindListItem, 2, "2.1 Literatura[17]"},
		{kindListItem, 1, "3 Przypisy[18]"},
		{kindHeading, 2, "Rodowód[edytuj[19]]"},
		{kindText, 0, "Był synem Ereba[20] i Nyks[21]. Przedstawiano go jako ponurego, brodatego"},
		{kindText, 0, "starca w łachmanach."},
		{kindImage, 0, "image[22]"},
		{kindText, 0, "Aleksandr Litowczenko, Charon przewozi dusze"},
		{kindHeading, 2, "W kulturze"},
		{kindHeading, 3, "Literatura"},
		{kindListItem, 1, "Wergiliusz[23], Eneida, księga VI"},
		{kindListItem, 1, "Dante[24], Boska komedia — „Piekło”, pieśń III"},
		{kindListItem, 1, "Lukian z Samosat, Rozmowy zmarłych"},
		{kindQuote, 1, "Przewoźnik strzeże tych wód i rzeki straszliwy, Charon, w ohydnym brudzie…"},
		{kindHeading, 2, "Przypisy"},
		{kindListItem, 1, "1. ↑[25] Pierre Grimal: Słownik mitologii greckiej i rzymskiej. Wrocław:"},
		{kindListItem, 1, "Ossolineum, 2008, s. 59."},
		{kindRule, 0, "----------"},
		{kindListItem, 1, "Tę stronę ostatnio edytowano 3 lis 2026, 10:21."},
		{kindListItem, 1, "Polityka prywatności[26]"},
	});
	if (g.linkCount == 26) {
		CheckEq(d.LinkUrl(3), "/wiki/Kategoria:Kategorie Główne", "article link url with entities");
		char baseUrl[128], url[256], fragment[64];
		size_t baseLength = 0, fragmentLength = 0;
		const char* doc = "https://pl.wikipedia.org/wiki/Charon_(mitologia)";
		PageBaseUrl(g, doc, strlen(doc), baseUrl, sizeof baseUrl, &baseLength);
		ResolveLink(g, 3, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://pl.wikipedia.org/wiki/Kategoria:Kategorie%20G%C5%82%C3%B3wne", "article link resolved");
		ResolveLink(g, 4, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://pl.wikipedia.org/wiki/Plik:Charon.jpg", "image link resolved");
		ResolveLink(g, 25, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://foundation.wikimedia.org/wiki/Privacy_policy", "scheme-relative link resolved");
		// table of contents: fragment link -> anchor -> heading line
		Check(ResolveLink(g, 15, baseUrl, baseLength, url, sizeof url, nullptr, fragment,
			sizeof fragment, &fragmentLength), "fragment link resolves");
		CheckEq(fragment, "W_kulturze", "fragment of a contents link");
		Check(FindAnchor(g, fragment, fragmentLength) == 25, "contents link lands on its heading");
		ResolveLink(g, 14, baseUrl, baseLength, url, sizeof url, nullptr, fragment, sizeof fragment,
			&fragmentLength);
		CheckEq(fragment, "Rodow%C3%B3d", "non-ASCII fragment is percent-encoded");
		Check(FindAnchor(g, fragment, fragmentLength) == 20, "percent-encoded fragment finds the anchor");
		Check(FindAnchor(g, "cite_note-1", 11) == 32 && FindAnchor(g, "cite_ref-1", 10) == 13,
			"footnote anchors both ways");
		Check(g.links[4].line == 8 && g.links[12].line == 13, "article link lines");
		char search[256];
		Check(g.formCount == 1 && BuildFormGetUrl(g, 0, baseUrl, baseLength, nullptr, kNone, search,
			sizeof search), "article search form");
		CheckEq(search, "https://pl.wikipedia.org/w/index.php?search=&title=Specjalna%3ASzukaj",
			"article search url");
	} else {
		Check(false, "article has 26 links");
	}
	Check(FindNextHeading(g, 0, 1) == 6 && FindNextHeading(g, 7, 1, 2) == 15
		&& FindNextHeading(g, 27, 1) == 31 && FindNextHeading(g, 30, -1, 3) == 26, "article headings");

	Check(d.Html(kSearch), "search results page renders");
	Expect(d, "search results page", {
		{kindForm, 0, "edit q: charon uefi"},
		{kindForm, 0, "list df: Ostatni tydzień"},
		{kindForm, 0, "check box safe: 1 checked"},
		{kindText, 0, "Filtr rodzinny"},
		{kindForm, 0, "button Szukaj"},
		{kindTableRow, 0, "1. | Charon – mówiący menedżer rozruchu UEFI[1]"},
		{kindTableRow, 0, "Menedżer rozruchu z wbudowanym czytnikiem ekranu: czyta menu GRUB, rEFInd i"},
		{kindTableRow, 0, "Windows Boot Manager."},
		{kindTableRow, 0, "example.org/charon"},
		{kindTableRow, 0, "2. | Charon (mitologia) – Wikipedia[2]"},
		{kindTableRow, 0, "Charon – w mitologii greckiej przewoźnik dusz."},
		{kindForm, 0, "button Następna strona >"},
		{kindText, 0, "Opinia[3] | O nas[4]"},
	});
	CheckEq(d.Title(), "charon uefi at Szukacz", "search page title");
	if (d.page.linkCount == 4 && d.page.formCount == 2) {
		const Page& r = d.page;
		const char* doc = "https://html.duckduckgo.com/html/?q=stare";
		char baseUrl[128], url[256];
		size_t baseLength = 0;
		PageBaseUrl(r, doc, strlen(doc), baseUrl, sizeof baseUrl, &baseLength);
		ResolveLink(r, 0, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.org%2Fcharon&rut=abc", "result link");
		ResolveLink(r, 1, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://pl.wikipedia.org/wiki/Charon_(mitologia)", "unquoted absolute link");
		ResolveLink(r, 2, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://html.duckduckgo.com/feedback?q=charon%20uefi", "link with a space");
		ResolveLink(r, 3, baseUrl, baseLength, url, sizeof url);
		CheckEq(url, "https://html.duckduckgo.com/about", "../ link");
		// the user types a new query into field q and presses the button
		std::vector<FieldValue> values(r.fieldCount, FieldValue{nullptr, 0});
		const char typed[] = "czytnik ekranu UEFI \"Charon\"";
		Check(r.lines[0].field == 0 && r.fields[0].type == fieldText, "query field is field 0");
		values[0] = {typed, strlen(typed)};
		Check(BuildFormGetUrl(r, 0, baseUrl, baseLength, values.data(), r.lines[4].field, url, sizeof url),
			"search form submits by GET");
		CheckEq(url, "https://html.duckduckgo.com/html/?q=czytnik+ekranu+UEFI+%22Charon%22&kl=pl-pl&s=0&df=w&safe=1",
			"search form url");
		Check(!BuildFormGetUrl(r, 1, baseUrl, baseLength, nullptr, kNone, url, sizeof url),
			"the POST form is reported as such");
	} else {
		Check(false, "search page has 4 links and 2 forms");
	}
}

static void TestOverflow()
{
	Section("pool overflow");
	const std::string page = std::string(kWiki) + kSearch;
	Doc full;
	Check(full.Html(page), "reference render");
	const Page& f = full.page;
	const uint32_t need[7] = {f.textCount, f.lineCount, f.linkCount, f.stringCount, f.formCount,
		f.fieldCount, f.anchorCount};
	const uint8_t bit[7] = {overflowText, overflowLines, overflowLinks, overflowStrings,
		overflowForms, overflowFields, overflowAnchors};
	static const char* names[7] = {"text pool overflow", "line array overflow", "link array overflow",
		"string pool overflow", "form array overflow", "field array overflow",
		"anchor array overflow is not fatal"};
	printf("  reference: %u text units, %u lines, %u links, %u string bytes, %u forms, %u fields, "
		"%u anchors from %zu bytes\n", need[0], need[1], need[2], need[3], need[4], need[5], need[6],
		page.size());
	for (int pool = 0; pool < 7; pool++) {
		bool ok = need[pool] > 0;
		int runs = 0;
		for (uint32_t cap = 0; cap <= need[pool] && ok; cap += (need[pool] > 600 && cap > 40
				&& cap + 60 < need[pool]) ? 37 : 1) {
			uint32_t c[7] = {1 << 16, 4096, 1024, 1 << 16, 16, 256, 256};
			c[pool] = cap;
			Doc d(c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
			const bool complete = d.Html(page);
			std::string why;
			runs++;
			if (!Valid(d, &why)) {
				printf("  capacity %u: %s\n", cap, why.c_str());
				ok = false;
			}
			const bool exact = cap == need[pool];
			if (pool == 6) {
				// anchors: the page itself is complete and identical
				if (!complete || d.page.lineCount != f.lineCount || d.page.textCount != f.textCount
					|| ((d.page.overflow & overflowAnchors) != 0) != (cap != 0 && !exact))
					ok = false;
				continue;
			}
			if (complete != exact || d.page.truncated == exact
				|| (!exact && d.page.overflow != bit[pool]) || (exact && d.page.overflow)) {
				printf("  capacity %u: complete %d overflow %02x\n", cap, complete, d.page.overflow);
				ok = false;
			}
			// what was rendered before the overflow is the start of the full page
			if (d.page.lineCount > f.lineCount || d.page.linkCount > f.linkCount)
				ok = false;
			for (uint32_t i = 0; ok && i + 1 < d.page.lineCount; i++)
				if (d.LineText(i) != full.LineText(i) || d.page.lines[i].kind != f.lines[i].kind)
					ok = false;
			for (uint32_t i = 0; ok && i < d.page.linkCount; i++)
				if (d.LinkUrl(i) != full.LinkUrl(i) || d.page.links[i].line != f.links[i].line)
					ok = false;
			if (!ok)
				printf("  capacity %u of %u\n", cap, need[pool]);
		}
		Check(ok && runs > 2, names[pool]);
	}
	// everything too small at once, and no buffers at all
	Doc small(64, 3, 1, 32, 1, 1, 1);
	Check(!small.Html(page) && small.page.truncated && Valid(small), "all pools tiny");
	Doc nothing(0, 0, 0, 0, 0, 0, 0);
	Check(!nothing.Html(page) && nothing.page.truncated && nothing.page.lineCount == 0
		&& Valid(nothing), "no buffers at all");
	Check(nothing.Html("") && nothing.Html("<p>  <br></p><script>x</script>"),
		"an empty page needs no buffers");
	Doc noLinks(1 << 16, 4096, 1024, 0, 0, 0, 0);
	Check(noLinks.Html("<h1>T</h1><p>tekst <b>bez</b> odnośników<ul><li>a<li>b</ul><span class=x>s</span>")
		&& noLinks.page.lineCount == 5, "text-only page needs no string pool");
}

// ---- fuzz ----------------------------------------------------------------

static uint64_t gSeed = 0x9E3779B97F4A7C15ull;
static uint32_t Random()
{
	gSeed ^= gSeed << 13;
	gSeed ^= gSeed >> 7;
	gSeed ^= gSeed << 17;
	return uint32_t(gSeed >> 16);
}
static uint32_t Random(uint32_t below) { return below ? Random() % below : 0; }

static void TestFuzz(int iterations)
{
	Section("fuzz");
	static const char* pieces[] = {"<", ">", "</", "/>", "\"", "'", "=", "&", "&#", "&#x", ";", "<!--", "-->",
		"<![CDATA[", "]]>", "<a href=", "</a>", "<table>", "<tr>", "<td>", "</table>", "<ul>", "<ol start=",
		"<li>", "</ul>", "<pre>", "</pre>", "<select>", "<option>", "</select>", "<textarea>", "<form>",
		"</form>", "<input ", "<button>", "</button>", "<script>", "</script>", "<style>", "<title>",
		"<h1>", "</h6>", "<blockquote>", "<img alt=", "<hr>", "<br>", "<p>", "<div id=", "\n", "\t", " ",
		"\xC5", "\xF0\x9F", "\xEF\xBB\xBF", "\x01", "&nbsp;", "&amp", "&#1114112;", "&#xD800;",
		"<base href=", "<meta charset=iso-8859-2>",
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"<svg>", "<optgroup>", "<caption>", "<th>", "value=", "type=checkbox ", "type=hidden ", "name=n "};
	const size_t pieceCount = sizeof pieces / sizeof pieces[0];
	const std::string samples[3] = {kWiki, kSearch,
		"<form action=/a><input name=q value=1><select name=s><option value=1>a<option selected>b</select>"
		"<textarea name=t>x\ny</textarea><input type=checkbox name=c checked><button name=b value=2>Go</button>"
		"</form><pre>  a\n\tb\n</pre><table><tr><td>1<td>2<tr><td><ul><li>x<ol><li>y</ol></ul></table>"
		"<a href='../x y#f'><div>blok</div></a><blockquote><p>cytat<blockquote>dwa</blockquote></blockquote>"
		"<img alt='obraz'><a href=q><img src=x></a>&#x1F600;&oacute;&copy 2026"};
	int failures = 0, truncated = 0;
	uint64_t lines = 0;
	for (int it = 0; it < iterations && failures < 5; it++) {
		const uint64_t seed = gSeed;
		std::string html = samples[Random(3)];
		const uint32_t mutations = 1 + Random(8);
		for (uint32_t m = 0; m < mutations; m++) {
			const uint32_t at = Random(uint32_t(html.size()) + 1);
			switch (Random(7)) {
			case 0:  // change a byte
				if (at < html.size()) html[at] = char(Random(256));
				break;
			case 1:  // delete a run
				html.erase(at, Random(40));
				break;
			case 2:  // insert a piece of markup
			case 3:
				html.insert(at, pieces[Random(uint32_t(pieceCount))]);
				break;
			case 4:  // cut the document
				if (Random(4) == 0) html.resize(at);
				break;
			case 5: {  // repeat a run somewhere else
				const std::string run = html.substr(at, Random(200));
				html.insert(Random(uint32_t(html.size()) + 1), run);
				break;
			}
			default: {  // random bytes
				std::string noise;
				for (uint32_t i = Random(12); i > 0; i--) noise += char(Random(256));
				html.insert(at, noise);
				break;
			}
			}
		}
		uint32_t c[7] = {1 << 15, 2048, 512, 1 << 15, 8, 128, 128};
		if (Random(2)) {
			static const uint32_t top[7] = {6000, 200, 40, 3000, 3, 12, 20};
			for (int i = 0; i < 7; i++)
				if (Random(3) == 0)
					c[i] = Random(top[i]);
		}
		Doc d(c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
		static const uint32_t widths[] = {78, 0, 1, 16, 17, 20, 40, 80, 132};
		d.options.width = widths[Random(9)];
		d.options.marker = Marker(Random(3));
		d.options.skipUnlabelledImages = Random(2) != 0;
		d.options.words.bullet = Random(2) ? u"*" : nullptr;
		const Charset hint = Charset(Random(6));
		if (Random(8) == 0)
			d.Plain(html, hint);
		else
			d.Html(html, hint);
		std::string why;
		if (!Valid(d, &why)) {
			printf("  iteration %d seed %llx: %s\n", it, static_cast<unsigned long long>(seed), why.c_str());
			failures++;
			continue;
		}
		truncated += d.page.truncated;
		lines += d.page.lineCount;
		// the helpers on whatever came out
		const int before = failures;
		const Page& g = d.page;
		const uint32_t from = Random(g.lineCount + 2);
		const int direction = Random(2) ? 1 : -1;
		uint32_t r = FindNextHeading(g, from, direction, Random(7));
		if (r != kNone && (r >= g.lineCount || g.lines[r].kind != kindHeading)) failures++;
		r = FindNextLink(g, from, direction);
		if (r != kNone && (r >= g.lineCount || !g.lines[r].linkCount)) failures++;
		r = FindNextField(g, from, direction);
		if (r != kNone && (r >= g.lineCount || g.lines[r].kind != kindForm)) failures++;
		if (g.textCount) {
			const uint32_t start = Random(g.textCount), length = 1 + Random(6);
			if (start + length <= g.textCount) {
				uint32_t column = 0;
				r = FindText(g, g.text + start, length, from, direction, &column);
				if (r != kNone && (r >= g.lineCount || column > g.lines[r].length)) failures++;
				// a needle taken from the page is found from the top
				const uint32_t textEnd = g.lineCount
					? g.lines[g.lineCount - 1].start + g.lines[g.lineCount - 1].length : 0;
				if (start + length <= textEnd && FindText(g, g.text + start, length, 0, 1) == kNone)
					failures++;
			}
		}
		const char* base = "http://example.com/a/b/c?d#e";
		for (uint32_t i = 0; i < g.linkCount; i++) {
			const size_t cap = Random(4) ? 600 : Random(60);
			std::vector<char> out(cap + 1, '#'), fragment(cap / 2 + 1, '#');
			size_t n = 0, fn = 0;
			const bool ok = ResolveLink(g, i, base, strlen(base), out.data(), cap, &n, fragment.data(),
				cap / 2, &fn);
			if (out[cap] != '#' || fragment[cap / 2] != '#' || (ok && (n >= cap || out[n] != 0))
				|| (ok && fn && fn >= cap / 2))
				failures++;
			if (ok && fn)
				FindAnchor(g, fragment.data(), fn);
		}
		for (uint32_t i = 0; i < g.formCount; i++) {
			const size_t cap = Random(4) ? 2000 : Random(80);
			std::vector<char> out(cap + 1, '#');
			size_t n = 0;
			const uint32_t submit = g.fieldCount ? Random(g.fieldCount) : kNone;
			BuildFormGetUrl(g, i, base, strlen(base), nullptr, submit, out.data(), cap, &n);
			if (out[cap] != '#' || n > cap || (cap && (n >= cap || out[n] != 0)))
				failures++;
		}
		for (uint32_t i = 0; i < g.anchorCount; i++)
			if (g.lineCount && LineAt(g, g.anchors[i].position) >= g.lineCount)
				failures++;
		if (failures != before)
			printf("  iteration %d seed %llx: helper check failed\n", it,
				static_cast<unsigned long long>(seed));
	}
	printf("  %d mutated pages, %d truncated, %llu lines\n", iterations, truncated,
		static_cast<unsigned long long>(lines));
	Check(failures == 0, "fuzz: every mutated page is valid and the helpers stay in bounds");
	// noise and pathological repetition
	bool ok = true;
	for (int it = 0; it < iterations / 8 && ok; it++) {
		std::string noise;
		const uint32_t length = Random(3000);
		const uint32_t mode = Random(4);
		for (uint32_t i = 0; i < length; i++)
			noise += mode == 0 ? char(Random(256)) : mode == 1 ? "<>&;#x\"'= /a-!"[Random(14)]
				: mode == 2 ? pieces[Random(uint32_t(pieceCount))][0] : char(32 + Random(95));
		if (mode == 3)
			for (uint32_t i = Random(60); i > 0; i--)
				noise.insert(Random(uint32_t(noise.size()) + 1), pieces[Random(uint32_t(pieceCount))]);
		Doc d(Random(2) ? 4096 : Random(500), 64 + Random(64), Random(16), Random(2) ? 4096 : Random(300),
			Random(4), Random(16), Random(16));
		d.options.width = Random(3) ? 40 : 0;
		d.Html(noise, Charset(Random(6)));
		std::string why;
		if (!Valid(d, &why)) {
			printf("  noise iteration %d: %s\n", it, why.c_str());
			ok = false;
		}
	}
	Check(ok, "fuzz: noise");
}

int main(int argc, char** argv)
{
	const int iterations = argc > 1 ? atoi(argv[1]) : 4000;
	TestBasics();
	TestLinks();
	TestWrap();
	TestForms();
	TestBroken();
	TestEntities();
	TestCharsets();
	TestUrls();
	TestFind();
	TestPlain();
	TestRealistic();
	TestOverflow();
	TestFuzz(iterations);
	for (int i = 2; i < argc; i++) {
		FILE* file = fopen(argv[i], "rb");
		if (!file) {
			printf("cannot open %s\n", argv[i]);
			continue;
		}
		std::string bytes;
		char block[4096];
		for (size_t n; (n = fread(block, 1, sizeof block, file)) > 0;)
			bytes.append(block, n);
		fclose(file);
		const uint32_t size = uint32_t(bytes.size());
		Doc d(size + 4096, size / 16 + 256, size / 64 + 64, size / 2 + 4096, 64, 1024, 8192);
		d.Html(bytes);
		printf("---- %s: %u bytes -> %u text units, %u lines, %u links, %u string bytes, %u forms, "
			"%u fields, %u anchors%s, title |%s|\n", argv[i], size, d.page.textCount, d.page.lineCount,
			d.page.linkCount, d.page.stringCount, d.page.formCount, d.page.fieldCount,
			d.page.anchorCount, d.page.truncated ? " TRUNCATED" : "", d.Title().c_str());
		std::string why;
		Check(Valid(d, &why), "file renders to a valid page");
		d.Dump();
	}
	printf("%d checks, %d failures\n", gChecks, gFailures);
	if (gFailures)
		return 1;
	printf("PASS\n");
	return 0;
}
