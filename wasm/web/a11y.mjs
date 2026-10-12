/*
 * Copyright (C) 2004-2026 The thinksynth authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * a11y.mjs -- what axe-core finds wrong with a page, for the harnesses
 * that have one open.
 *
 * WCAG 2.2 AA's rules. Contrast is left out until the page's colors are
 * reworked, and anything a caller passes in `exclude' is not looked at:
 * the composer's controls, which are being rebuilt.
 */

import { AxeBuilder } from '@axe-core/playwright';

/* One line per rule broken, naming the first few elements that break it;
   none for a page that passes. `page' must come from browser.newContext(),
   which axe needs to inject itself into every frame. */
export async function axeFindings (page, exclude = [])
{
    let builder = new AxeBuilder({ page })
        .withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'])
        .disableRules(['color-contrast']);

    for (const selector of exclude)
        builder = builder.exclude(selector);

    const { violations } = await builder.analyze();

    return violations.map((v) => `${v.id}: ` +
        v.nodes.slice(0, 3).map((n) => n.target.join(' ')).join(', ') +
        (v.nodes.length > 3 ? `, and ${v.nodes.length - 3} more` : ''));
}
