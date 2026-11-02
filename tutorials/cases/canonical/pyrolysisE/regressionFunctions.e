// pyrolysisE only: gas T and p, which the e and h forms must agree on.
regressionTAverage
{
    ${regressionBase}
    enabled         true;
    fields          (T p);
    operation       volAverage;
}
