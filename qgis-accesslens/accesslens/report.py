"""Self-contained HTML/SVG reports with escaped labels and no network requests."""

from html import escape


def number(value, digits=0):
    return "N/A" if value is None else f"{value:,.{digits}f}"


def map_svg(result):
    points = [row.demand for row in result.rows] + list(result.facilities)
    radius = result.threshold_m
    min_x = min(point.x for point in points) - radius
    max_x = max(point.x for point in points) + radius
    min_y = min(point.y for point in points) - radius
    max_y = max(point.y for point in points) + radius
    scale = min(880 / (max_x - min_x), 540 / (max_y - min_y))
    ox = (960 - (max_x - min_x) * scale) / 2
    oy = (620 - (max_y - min_y) * scale) / 2

    def xy(point):
        return ox + (point.x - min_x) * scale, 620 - oy - (point.y - min_y) * scale

    elements = [
        '<svg viewBox="0 0 960 620" role="img" aria-labelledby="map-title map-desc" xmlns="http://www.w3.org/2000/svg">',
        '<title id="map-title">Community access to service locations</title>',
        '<desc id="map-desc">Teal points are within the distance threshold; coral points are outside. Circles show straight-line service radii. Diamond markers show facilities.</desc>',
        '<defs><pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse"><path d="M40 0H0V40" fill="none" stroke="#dce6e6" stroke-width="0.7"/></pattern></defs>',
        '<rect width="960" height="620" rx="16" fill="#f2f7f5"/>',
        '<rect width="960" height="620" rx="16" fill="url(#grid)"/>',
    ]
    for facility in result.facilities:
        x, y = xy(facility)
        elements.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{radius * scale:.2f}" fill="#168a7b" fill-opacity="0.08" stroke="#168a7b" stroke-opacity="0.3" stroke-dasharray="5 5"/>')
    largest = max(row.demand.population for row in result.rows) or 1
    for row in result.rows:
        x, y = xy(row.demand)
        fx, fy = xy(row.facility)
        color = "#157e73" if row.served else "#d05b46"
        elements.append(f'<path d="M{x:.2f} {y:.2f}L{fx:.2f} {fy:.2f}" stroke="{color}" opacity="0.25" stroke-width="1.2"/>')
        size = 4 + 7 * (row.demand.population / largest) ** 0.5
        title = escape(f"{row.demand.name}: {row.demand.population:g} people; {row.distance_m:.0f} m; {'served' if row.served else 'underserved'}")
        elements.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{size:.2f}" fill="{color}" stroke="white" stroke-width="1.6"><title>{title}</title></circle>')
    for i, facility in enumerate(result.facilities):
        x, y = xy(facility)
        elements.append(f'<path d="M{x:.2f} {y-10:.2f}l10 10 -10 10 -10 -10Z" fill="#183c4a" stroke="white" stroke-width="2"><title>{escape(facility.name)}</title></path>')
        elements.append(f'<text x="{x+15:.2f}" y="{y+4:.2f}" font-family="sans-serif" font-size="12" font-weight="700" fill="#183c4a">F{i+1}</text>')
    bar_m = 1000 if max_x - min_x > 4000 else 250
    elements.extend([
        '<path d="M900 70V30l-5 12m5-12 5 12" stroke="#183c4a" stroke-width="2" fill="none"/>',
        '<text x="900" y="22" text-anchor="middle" fill="#183c4a" font-family="sans-serif" font-size="12">Grid N</text>',
        f'<path d="M40 570v8h{bar_m*scale:.2f}v-8" stroke="#183c4a" fill="none" stroke-width="2"/>',
        f'<text x="40" y="600" fill="#183c4a" font-family="sans-serif" font-size="12">{bar_m:,} m · local projected grid</text>',
        '</svg>',
    ])
    return "\n".join(elements)


def render_report(result, crs, dataset="User-provided data"):
    summary = result.summary()
    priorities = sorted((row for row in result.rows if not row.served), key=lambda r: r.gap_rank)
    table = "".join(
        f'<tr><td>{row.gap_rank:02d}</td><td>{escape(row.demand.name)}</td>'
        f'<td>{number(row.demand.population)}</td><td>{number(row.distance_m)}</td>'
        f'<td>{number(row.priority, 1)}</td></tr>' for row in priorities[:10]
    ) or '<tr><td colspan="5">All communities are within the selected threshold.</td></tr>'
    scenarios = "".join(
        f'<tr><td>{number(t)} m</td><td>{number(pop)}</td><td>{number(pct, 1)}%</td></tr>'
        for t, pop, pct in result.scenarios([result.threshold_m / 2, result.threshold_m, result.threshold_m * 1.5])
    )
    facilities = " · ".join(f"F{i+1}: {escape(f.name)}" for i, f in enumerate(result.facilities))
    coverage = "N/A" if summary["coverage_pct"] is None else number(summary["coverage_pct"], 1) + "%"
    return f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AccessLens — Service access analysis</title>
<style>
:root{{color-scheme:light;--ink:#163744;--muted:#536d77;--teal:#157e73;--line:#dce7e6}}
*{{box-sizing:border-box}}body{{margin:0;background:#f4f6f3;color:var(--ink);font:15px/1.6 system-ui,-apple-system,Segoe UI,sans-serif}}
main{{max-width:1240px;margin:auto;padding:44px 36px}}header{{display:flex;justify-content:space-between;gap:24px;align-items:center;border-bottom:1px solid var(--line);padding-bottom:24px}}
.brand{{font-weight:800;font-size:24px;letter-spacing:-1px}}.brand span{{color:var(--teal)}}.pill{{font-size:11px;letter-spacing:1.5px;border:1px solid #b3cbc5;border-radius:30px;padding:8px 14px}}
.eyebrow{{color:var(--teal);font-size:12px;letter-spacing:2px;font-weight:700;margin-top:34px}}h1{{font-size:clamp(30px,4vw,49px);letter-spacing:-2px;line-height:1.1;margin:12px 0}}p{{color:var(--muted)}}
.metrics{{display:grid;grid-template-columns:repeat(4,1fr);gap:16px;margin:28px 0}}.metric,article{{background:white;border:1px solid var(--line);border-radius:14px;padding:22px}}
.metric strong{{font-size:32px;display:block;line-height:1.2;margin:10px 0;color:var(--teal)}}.metric small,.caption{{color:var(--muted);font-size:12px}}
.layout{{display:grid;grid-template-columns:1.65fr 1fr;gap:22px}}h2{{font-size:19px;margin:0 0 16px}}svg{{display:block;width:100%;height:auto}}.legend{{display:flex;gap:16px;flex-wrap:wrap;font-size:12px;margin:12px 0}}.dot{{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:var(--teal)}}.gap{{background:#d05b46}}.facility{{background:#183c4a;border-radius:0;transform:rotate(45deg)}}
table{{width:100%;border-collapse:collapse;font-size:12px}}th,td{{text-align:right;padding:11px 7px;border-bottom:1px solid var(--line)}}th:first-child,td:first-child,th:nth-child(2),td:nth-child(2){{text-align:left}}th{{font-size:10px;text-transform:uppercase;letter-spacing:0.5px;color:var(--muted)}}.note{{font-size:13px;background:#eef5f1;border-radius:10px;padding:16px;margin-top:20px}}.bottom{{margin-top:22px}}footer{{font-size:12px;color:var(--muted);padding:24px 0}}@media(max-width:900px){{.layout{{grid-template-columns:1fr}}.metrics{{grid-template-columns:repeat(2,1fr)}}main{{padding:24px 16px}}}}@media print{{body{{background:white}}main{{padding:0}}article,.metric{{break-inside:avoid}}.layout{{display:block}}article{{margin-bottom:16px}}}}
</style></head><body><main>
<header><div class="brand">Access<span>Lens</span> / QGIS</div><div class="pill">SERVICE ACCESS / ANALYSIS REPORT</div></header>
<div class="eyebrow">SPATIAL ANALYSIS · POPULATION COVERAGE</div>
<h1>Find the gaps.<br>Make access visible.</h1>
<p>{escape(dataset)} · {summary['communities']} communities · {summary['facilities']} facilities · {number(result.threshold_m)} m access threshold</p>
<section class="metrics" aria-label="Summary metrics">
<div class="metric">Population within reach<strong>{coverage}</strong><small>Population-weighted coverage</small></div>
<div class="metric">People outside threshold<strong>{number(summary['underserved_population'])}</strong><small>Of {number(summary['population'])} total people</small></div>
<div class="metric">Communities underserved<strong>{summary['underserved_communities']}</strong><small>Of {summary['communities']} communities assessed</small></div>
<div class="metric">Mean distance<strong>{number(summary['weighted_mean_m'])} m</strong><small>Weighted by population</small></div>
</section><section class="layout"><article><h2>Service access map</h2>{map_svg(result)}
<div class="legend"><span><i class="dot"></i>Within threshold</span><span><i class="dot gap"></i>Underserved</span><span><i class="dot facility"></i>Facility</span></div>
<p class="caption">Point size reflects population. Dashed circles show each facility's service radius; lines connect communities to their nearest facility. This is a schematic map without a basemap.</p>
<p class="caption">{facilities}</p></article>
<article><h2>Priority access gaps</h2><p class="caption">Top ten by population × excess distance (person-km).</p>
<table><thead><tr><th>Rank</th><th>Community</th><th>People</th><th>Metres</th><th>Score</th></tr></thead><tbody>{table}</tbody></table>
<div class="note"><strong>How to read priority</strong><br>Higher scores indicate larger population-distance gaps. These ranks support investigation; they do not identify an optimal new facility location.</div></article></section>
<article class="bottom"><h2>How the distance target changes coverage</h2><table><thead><tr><th>Threshold</th><th>Population served</th><th>Coverage</th></tr></thead><tbody>{scenarios}</tbody></table></article>
<article class="bottom"><h2>Method &amp; interpretation</h2><p>Each community is assigned to its nearest facility using straight-line distance in {escape(crs)}. A community is served when its distance is at most {number(result.threshold_m)} m. All of its population is represented at a single point. Population coverage is served population divided by total population; it is undefined when total population is zero.</p><p>Distances use a local projected grid. They do not represent road routes, travel times, opening hours, facility capacity, affordability, or real-world service quality. Choose an appropriate local metre-based CRS before interpreting results. Service circles may overlap and should not be summed as area coverage.</p></article>
<footer>AccessLens 0.1.0 · Reproducible GIS portfolio project · {escape(dataset)} · CRS: {escape(crs)}</footer>
</main></body></html>'''
