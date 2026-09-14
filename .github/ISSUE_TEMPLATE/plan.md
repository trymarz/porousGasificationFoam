---
name: Plan
about: A stable problem/objective/deliverables spec — not a running log
title: "Plan: "
labels: ""
assignees: ""
---

## Problem

Why is this needed? What's broken or missing?

## Objective / Deliverables

What "done" looks like. Concrete outcomes, not implementation steps.

## Plan

One checkbox per block/deliverable, each with a rough time estimate. Check
a box off when that block actually lands — this is the only part of this
issue meant to be touched routinely.

- [ ] … (~)
- [ ] … (~)

## Working details

Full implementation detail — files to touch, approach, step-by-step log,
work reports, dead ends — lives in the linked `/plans` file, not here:

`/plans/github.com/trymarz/porousGasificationFoam/<active-or-backlog>/<id>.md`

The branch that resolves this issue should be created via this issue's
**Development** panel ("Create a branch") so it's linked here automatically.

---

**This issue is a stable spec, not a status feed.** Once created, edit the
Problem/Objective/Deliverables sections only when work on the linked plan
reveals the original assumptions were wrong — not for routine progress
updates (those are checklist ticks, or belong in `/plans` instead). Apply a
`domain:*` label and add this issue to the PGF development project board
after creating it.
