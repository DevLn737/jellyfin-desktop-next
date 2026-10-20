export function renderMarkdown(report) {
  const lines = [
    '# SyncPlay harness report',
    '',
    `Generated ${report.generatedAt} against Jellyfin ${report.server.version}. Overall: **${report.passed ? 'PASS' : 'FAIL'}**.`,
    '',
    '| Scenario | Result | Checks |',
    '| --- | --- | --- |',
  ];
  for (const s of report.scenarios) {
    const failed = s.checks.filter((c) => !c.passed).length;
    const known = s.checks.filter((c) => c.knownIssue).length;
    lines.push(`| ${s.name} | ${s.passed ? 'PASS' : 'FAIL'} | ${s.checks.length - failed - known}/${s.checks.length}${known ? `, ${known} known issue` : ''}${s.error ? ' (error)' : ''} |`);
  }
  for (const s of report.scenarios) {
    lines.push('', `## ${s.name}: ${s.title}`, '');
    if (s.error) lines.push('```', s.error.split('\n').slice(0, 6).join('\n'), '```', '');
    for (const c of s.checks) {
      const mark = c.knownIssue ? '⚠️ known issue' : c.passed ? '✅' : '❌';
      lines.push(`- ${mark} ${c.name}: ${c.detail}`);
      if (c.knownIssue) lines.push(`  - ${c.knownIssue}`);
      if (c.knownIssueFixed) lines.push(`  - Known issue no longer reproduces, remove it from KNOWN_ISSUES: ${c.knownIssueFixed}`);
    }
  }
  return `${lines.join('\n')}\n`;
}
