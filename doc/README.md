# Documentation

The table of contents of PGF's documentation. New to PGF? Start with the [README quick start](../README.md#quick-start); on a clean machine, [install](install.md) OpenFOAM and PGF first. Then [build your own case](case-guide.md#your-first-case).

Each group answers one question; inside a group, pages run from overview to detail. Where a page and the source disagree, the source wins. The section links below are **generated** by `utilities/gen-doc-toc.py` from each page's headings; edit only the page lines.

## Installing

How do I get PGF onto a machine?

- [Installing](install.md)
  - [1. OpenFOAM v2606](install.md#1-openfoam-v2606)
  - [2. PGF](install.md#2-pgf)
  - [3. DEM coupling (optional)](install.md#3-dem-coupling-optional)
  - [Checking that the coupling is in](install.md#checking-that-the-coupling-is-in)
  - [ParaView and Yade's VTK](install.md#paraview-and-yades-vtk)
  - [Pitfalls](install.md#pitfalls)
- [Apptainer images](containers.md)

## Running cases

How do I set up and run a case?

- [Tutorials](tutorials.md)
  - [Case families](tutorials.md#case-families)
- [Case guide](case-guide.md)
  - [Your first case](case-guide.md#your-first-case)
  - [Setting up a DEM case](case-guide.md#setting-up-a-dem-case)
  - [Tips for preparing new simulations](case-guide.md#tips-for-preparing-new-simulations)
- [Case reference](case-reference.md)
  - [Required initial fields](case-reference.md#required-initial-fields)
  - [Solid boundary conditions](case-reference.md#solid-boundary-conditions)
  - [`constant/chemistryProperties`](case-reference.md#constantchemistryproperties)
  - [`constant/solidThermophysicalProperties`](case-reference.md#constantsolidthermophysicalproperties)
  - [`constant/pyrolysisProperties`](case-reference.md#constantpyrolysisproperties)
  - [`constant/yadeProperties`](case-reference.md#constantyadeproperties)
  - [`constant/lambdaDict`](case-reference.md#constantlambdadict)
  - [`constant/heatTransferProperties`](case-reference.md#constantheattransferproperties)
  - [`constant/specieTransferProperties`](case-reference.md#constantspecietransferproperties)
  - [`constant/porosityProperties` (optional)](case-reference.md#constantporosityproperties-optional)
  - [`constant/radiationProperties`](case-reference.md#constantradiationproperties)
  - [`system/` files: key settings](case-reference.md#system-files-key-settings)
- [Viewing a case in ParaView](paraview.md)
  - [Use it](paraview.md#use-it)
  - [Compare two cases](paraview.md#compare-two-cases)
  - [Requirements](paraview.md#requirements)
  - [The views](paraview.md#the-views)
  - [Add views to a case](paraview.md#add-views-to-a-case)
  - [Relation to foamcli](paraview.md#relation-to-foamcli)

## The pgf tool

What can pgf do, and how does it work?

- [The pgf tool](pgf.md)
  - [Settings](pgf.md#settings)
  - [Builds](pgf.md#builds)
  - [Runs](pgf.md#runs)
  - [Output](pgf.md#output)
- [pgf reference](pgf-reference.md): every page of `pgf help`, generated

## The model

What physics does it solve?

- [Theory](theory.md)
  - [Model overview](theory.md#model-overview)
  - [Notation](theory.md#notation)
  - [Equations](theory.md#equations)
  - [DEM coupling](theory.md#dem-coupling)
- [DEM coupling](theory-DEM.md)
  - [The idea](theory-DEM.md#the-idea)
  - [Body size models](theory-DEM.md#body-size-models)
  - [Limitations](theory-DEM.md#limitations)

## How the code implements it

Where does each piece live, and in what order does it run?

- [Code map](code-map.md)
  - [Where each model lives](code-map.md#where-each-model-lives)
  - [Per-time-step flow](code-map.md#per-time-step-flow)
  - [Gas–solid energy terms](code-map.md#gassolid-energy-terms)
- [Solid step](solid-step.md)
  - [The stages](solid-step.md#the-stages)
  - [Exceptions to the table](solid-step.md#exceptions-to-the-table)
- [DEM exchange](dem-exchange.md)
  - [Running a coupled case](dem-exchange.md#running-a-coupled-case)
  - [One exchange, step by step](dem-exchange.md#one-exchange-step-by-step)
  - [Keeping time](dem-exchange.md#keeping-time)
  - [Start and stop](dem-exchange.md#start-and-stop)
  - [The fixtures' spring model](dem-exchange.md#the-fixtures-spring-model)
  - [Where it lives in the code](dem-exchange.md#where-it-lives-in-the-code)
  - [Limitations](dem-exchange.md#limitations)

## Developing PGF

How do I change it safely?

- [Contributing](../CONTRIBUTING.md)
  - [Branch Naming](../CONTRIBUTING.md#branch-naming)
  - [Issue Tracking](../CONTRIBUTING.md#issue-tracking)
  - [Merge Strategy](../CONTRIBUTING.md#merge-strategy)
  - [PR Titles](../CONTRIBUTING.md#pr-titles)
  - [PR Body](../CONTRIBUTING.md#pr-body)
  - [Documentation](../CONTRIBUTING.md#documentation)
  - [Private companion repository](../CONTRIBUTING.md#private-companion-repository)
- [Regression testing](regression-testing.md)
  - [Run it](regression-testing.md#run-it)
  - [Outcomes](regression-testing.md#outcomes)
  - [Add a case](regression-testing.md#add-a-case)
  - [How the comparison works](regression-testing.md#how-the-comparison-works)
  - [Limitations](regression-testing.md#limitations)
  - [Protocol reference](regression-testing.md#protocol-reference)
- [Regression metric ledger](regression-ledger.md)
  - [Status](regression-ledger.md#status)
  - [Ledger](regression-ledger.md#ledger)
- [The build system](build.md)
  - [How a build works](build.md#how-a-build-works)
  - [Where things land](build.md#where-things-land)
  - [Editing and rebuilding](build.md#editing-and-rebuilding)
  - [Switching variants](build.md#switching-variants)
  - [YADE DEM coupling](build.md#yade-dem-coupling)
  - [Cleaning](build.md#cleaning)
  - [Choosing targets](build.md#choosing-targets)
- [Developer tools](dev-tools.md)
  - [Editor navigation (clangd)](dev-tools.md#editor-navigation-clangd)
  - [Agent navigation (Serena)](dev-tools.md#agent-navigation-serena)
  - [API reference (Doxygen)](dev-tools.md#api-reference-doxygen)
