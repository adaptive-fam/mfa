//--------------------------------------------------------------
// decoder object
// ref: [P&T] Piegl & Tiller, The NURBS Book, 1995
//
// Tom Peterka
// Argonne National Laboratory
// tpeterka@mcs.anl.gov
//--------------------------------------------------------------

#include <mfa/mfa.hpp>
#include <mfa/decode.hpp>

#include <Eigen/Dense>

#include <vector>
#include <iostream>

using namespace std;

mfa::
Decoder::
Decoder(MFA& mfa_) :
    mfa(mfa_)
//     p(mfa_.p),
//     ndom_pts(mfa_.ndom_pts),
//     nctrl_pts(mfa_.nctrl_pts),
//     domain(mfa_.domain),
//     params(mfa_.params),
//     ctrl_pts(mfa_.ctrl_pts),
//     knots(mfa_.knots),
//     dom_range(mfa_.dom_range),
//     po(mfa_.po),
//     ko(mfa_.ko)
    // DEPRECATED
//     knot_spans(mfa_.knot_spans)
//     ndone_knot_spans(mfa_.ndone_knot_spans)
{
    // ensure that encoding was already done
    if (!mfa.p.size()         ||
        !mfa.ndom_pts.size()  ||
        !mfa.nctrl_pts.size() ||
        !mfa.domain.size()    ||
        !mfa.params.size()    ||
        !mfa.ctrl_pts.size()  ||
        !mfa.knots.size())
    {
        fprintf(stderr, "Decoder() error: Attempting to decode before encoding.\n");
        exit(0);
    }

    // initialize decoding data structures
    cs.resize(mfa.p.size(), 1);
    tot_iters = 1;                              // total number of iterations in the flattened decoding loop
    for (size_t i = 0; i < mfa.p.size(); i++)       // for all dims
    {
        tot_iters  *= (mfa.p(i) + 1);
        if (i > 0)
            cs[i] = cs[i - 1] * mfa.nctrl_pts[i - 1];
    }
    ct.resize(tot_iters, mfa.p.size());

    // compute coordinates of first control point of curve corresponding to this iteration
    // these are relative to start of the box of control points located at co
    for (int i = 0; i < tot_iters; i++)      // 1-d flattening all n-d nested loop computations
    {
        int div = tot_iters;
        int i_temp = i;
        for (int j = mfa.p.size() - 1; j >= 0; j--)
        {
            div      /= (mfa.p(j) + 1);
            ct(i, j) =  i_temp / div;
            i_temp   -= (ct(i, j) * div);
        }
    }

}

// DEPRECATED
// #if 1
// 
// // computes error in knot spans, tbb version
// // marks the knot spans that are done (error <= max_error in the entire span)
// // returns whether normalized error in all spans is below the err_limit
// bool
// mfa::
// Decoder::
// ErrorSpans(
//         VectorXi& nnew_knots,                       // number of new knots in each dim
//         VectorXf& new_knots,                        // new knots (1st dim changes fastest)
//         float err_limit)                            // max allowable error
// {
//     // spans that have already been split in this round (to prevent splitting twice)
//     vector<bool> split_spans(knot_spans.size());    // intialized to false by default
// 
//     parallel_for(size_t(0), knot_spans.size(), [&] (size_t i)          // knot spans
//     {
//         if (!knot_spans[i].done)
//         {
//             // debug
// //             fprintf(stderr, "ErrorSpans(): span %ld\n", i);
// 
//             size_t nspan_pts = 1;                                   // number of domain points in the span
//             for (auto k = 0; k < mfa.p.size(); k++)
//                 nspan_pts *= (knot_spans[i].max_param_ijk(k) - knot_spans[i].min_param_ijk(k) + 1);
// 
//             VectorXi p_ijk = knot_spans[i].min_param_ijk;           // indices of current parameter in the span
//             VectorXf param(mfa.p.size());                               // value of current parameter
//             bool span_done = true;                                  // span is done until error > err_limit
// 
//             // TODO:  consider binary search of the points in the span?
//             // (error likely to be higher in the center of the span?)
//             for (auto j = 0; j < nspan_pts; j++)                    // parameters in the span
//             {
//                 // debug
// //                 fprintf(stderr, "ErrorSpans(): span %ld point %d\n", i, j);
// 
//                 for (auto k = 0; k < mfa.p.size(); k++)
//                 param(k) = mfa.params(mfa.po[k] + p_ijk(k));
// 
//                 // approximate the point and measure error
//                 size_t idx;
//                 mfa.ijk2idx(p_ijk, idx);
//                 VectorXf cpt(mfa.ctrl_pts.cols());       // approximated point
//                 VolPt(param, cpt);
//                 float err = fabs(mfa.NormalDistance(cpt, idx)) / mfa.dom_range;     // normalized by data range
// 
//                 // span is not done
//                 if (err > err_limit)
//                 {
//                     span_done = false;
//                     break;
//                 }
// 
//                 // increment param ijk
//                 for (auto k = 0; k < mfa.p.size(); k++)                 // dimensions in the parameter
//                 {
//                     if (p_ijk(k) < knot_spans[i].max_param_ijk(k))
//                     {
//                         p_ijk(k)++;
//                         break;
//                     }
//                     else
//                         p_ijk(k) = knot_spans[i].min_param_ijk(k);
//                 }                                                   // dimension in parameter
//             }                                                       // parameters in the span
// 
//             if (span_done)
//                 knot_spans[i].done = true;
//         }                                                           // knot span not done
//     });                                                           // knot spans
// 
// 
//     // split spans that are not done
//     auto norig_spans = knot_spans.size();
//     for (auto i = 0; i < norig_spans; i++)                  // knot spans
//         if (!knot_spans[i].done && !split_spans[i])
//         {
//             // debug
// //             fprintf(stderr, "*** calling SplitSpan(%d) ***\n", i);
// 
//             SplitSpan(i, split_spans, nnew_knots, new_knots);
//             mfa.InsertKnots(nnew_knots, new_knots);
//         }
// 
//     // debug
// //     for (auto i = 0; i < knot_spans.size(); i++)                  // knot spans
// //     {
// //         cerr <<
// //             "span_idx="          << i                           <<
// //             "\nmin_knot_ijk:\n"  << knot_spans[i].min_knot_ijk  <<
// //             "\nmax_knot_ijk:\n"  << knot_spans[i].max_knot_ijk  <<
// //             "\nmin_knot:\n"      << knot_spans[i].min_knot      <<
// //             "\nmax_knot:\n"      << knot_spans[i].max_knot      <<
// //             "\nmin_param_ijk:\n" << knot_spans[i].min_param_ijk <<
// //             "\nmax_param_ijk:\n" << knot_spans[i].max_param_ijk <<
// //             "\nmin_param:\n"     << knot_spans[i].min_param     <<
// //             "\nmax_param:\n"     << knot_spans[i].max_param     <<
// //             "\n"                 << endl;
// //     }
// 
//     // check if all done
//     // NB: not doint simple counter of done spans because it would have to be locked between tasks
//     for (auto i = 0; i < knot_spans.size(); i++)
//         if (!knot_spans[i].done)
//             return false;
//     return true;
// }
// 
// #else
// 
// // computes error in knot spans, single-threaded version
// // marks the knot spans that are done (error <= max_error in the entire span)
// // returns whether normalized error in all spans is below the err_limit
// bool
// mfa::
// Decoder::
// ErrorSpans(
//         VectorXi& nnew_knots,                       // number of new knots in each dim
//         VectorXf& new_knots,                        // new knots (1st dim changes fastest)
//         float err_limit)                            // max allowable error
// {
//     // spans that have already been split in this round (to prevent splitting twice)
//     vector<bool> split_spans(knot_spans.size());    // intialized to false by default
// 
//     for (auto i = 0; i < knot_spans.size(); i++)                  // knot spans
//     {
//         if (knot_spans[i].done)
//         {
//             ndone_knot_spans++;
//             continue;
//         }
// 
//         size_t nspan_pts = 1;                                   // number of domain points in the span
//         for (auto k = 0; k < mfa.p.size(); k++)
//             nspan_pts *= (knot_spans[i].max_param_ijk(k) - knot_spans[i].min_param_ijk(k) + 1);
// 
//         VectorXi p_ijk = knot_spans[i].min_param_ijk;           // indices of current parameter in the span
//         VectorXf param(mfa.p.size());                               // value of current parameter
//         bool span_done = true;                                  // span is done until error > err_limit
// 
//         // TODO:  consider binary search of the points in the span?
//         // (error likely to be higher in the center of the span?)
//         for (auto j = 0; j < nspan_pts; j++)                    // parameters in the span
//         {
//             for (auto k = 0; k < mfa.p.size(); k++)
//                 param(k) = mfa.params(mfa.po[k] + p_ijk(k));
// 
//             // approximate the point and measure error
//             size_t idx;
//             mfa.ijk2idx(p_ijk, idx);
//             // DEPECATED: following test for checking each domain point only once changes results (for
//             // (worse) and doesn't save a lot time
// //             if (!mfa.err_ok[idx])                   // check each domain point at most one time
// //             {
//                 VectorXf cpt(mfa.ctrl_pts.cols());       // approximated point
//                 VolPt(param, cpt);
//                 float err = fabs(mfa.NormalDistance(cpt, idx)) / mfa.dom_range;     // normalized by data range
// 
//                 // span is not done
//                 if (err > err_limit)
//                 {
//                     span_done = false;
//                     break;
//                 }
// //                 else
// //                     mfa.err_ok[idx] = true;
// //             }
// 
//             // increment param ijk
//             for (auto k = 0; k < mfa.p.size(); k++)                 // dimensions in the parameter
//             {
//                 if (p_ijk(k) < knot_spans[i].max_param_ijk(k))
//                 {
//                     p_ijk(k)++;
//                     break;
//                 }
//                 else
//                     p_ijk(k) = knot_spans[i].min_param_ijk(k);
//             }                                                   // dimension in parameter
//         }                                                       // parameters in the span
// 
//         // span is done
//         if (span_done)
//         {
//             knot_spans[i].done = true;
//             ndone_knot_spans++;
//         }
//     }                                                           // knot spans
// 
//     // split spans that are not done
//     auto norig_spans = knot_spans.size();
//     for (auto i = 0; i < norig_spans; i++)                  // knot spans
//         if (!knot_spans[i].done && !split_spans[i])
//         {
//             // debug
// //             fprintf(stderr, "*** calling SplitSpan(%d) ***\n", i);
// 
//             SplitSpan(i, split_spans, nnew_knots, new_knots);
//             mfa.InsertKnots(nnew_knots, new_knots);
//         }
// 
//     // debug
// //     fprintf(stderr, "number knot spans after splitting= %ld\n", knot_spans.size());
// //     for (auto i = 0; i < knot_spans.size(); i++)                  // knot spans
// //     {
// //         cerr <<
// //             "span_idx="          << i                           <<
// //             "\nmin_knot_ijk:\n"  << knot_spans[i].min_knot_ijk  <<
// //             "\nmax_knot_ijk:\n"  << knot_spans[i].max_knot_ijk  <<
// //             "\nmin_knot:\n"      << knot_spans[i].min_knot      <<
// //             "\nmax_knot:\n"      << knot_spans[i].max_knot      <<
// //             "\nmin_param_ijk:\n" << knot_spans[i].min_param_ijk <<
// //             "\nmax_param_ijk:\n" << knot_spans[i].max_param_ijk <<
// //             "\nmin_param:\n"     << knot_spans[i].min_param     <<
// //             "\nmax_param:\n"     << knot_spans[i].max_param     <<
// //             "\n"                 << endl;
// //     }
// 
//     // check if all done
//     // NB: not tallying a simple count of number of done spans because the multithreaded version
//     // would require locking that count; this version done similarly for compatibility with
//     // multithreaded version
//     for (auto i = 0; i < knot_spans.size(); i++)
//         if (!knot_spans[i].done)
//             return false;
//     return true;
// }
// 
// #endif

// // splits a knot span into two
// void
// mfa::
// Decoder::
// SplitSpan(
//         size_t        si,                   // id of span to split
//         vector<bool>& split_spans,          // spans that were split already in this round, don't split these again
//         VectorXi&     nnew_knots,           // number of new knots in each dim
//         VectorXf&     new_knots)            // new knots (1st dim changes fastest)
// {
//     // check if span can be split (both halves would have domain points in its range)
//     // if not, check other split directions
//     int sd = knot_spans[si].last_split_dim;             // new split dimension
//     float new_knot;                                     // new knot value in the split dimension
//     size_t k;                                           // dimension
//     for (k = 0; k < mfa.p.size(); k++)
//     {
//         sd       = (sd + 1) % mfa.p.size();
//         new_knot = (knot_spans[si].min_knot(sd) + knot_spans[si].max_knot(sd)) / 2;
//         if (mfa.params(mfa.po[sd] + knot_spans[si].min_param_ijk(sd)) < new_knot &&
//                 mfa.params(mfa.po[sd] + knot_spans[si].max_param_ijk(sd)) > new_knot)
//             break;
//     }
// 
//     if (k == mfa.p.size())                                  // a split direction could not be found
//     {
//         knot_spans[si].done = true;
//         split_spans[si]     = true;
//         // debug
// //         fprintf(stderr, "--- SplitSpan(): span %ld could not be split further ---\n", si);
//         return;
//     }
// 
//     // find all spans with the same min_knot_ijk as the span to be split and that are not done yet
//     // those will be split too (NB, in the same dimension as the original span to be split)
//     for (auto j = 0; j < split_spans.size(); j++)       // original number of spans in this round
//     {
//         if (knot_spans[j].done || split_spans[j] || knot_spans[j].min_knot_ijk(sd) != knot_spans[si].min_knot_ijk(sd))
//             continue;
// 
//         // debug
// //         fprintf(stderr, "+++ SplitSpan(): splitting span %ld +++\n", si);
// 
//         // copy span to the back
//         knot_spans.push_back(knot_spans[j]);
// 
//         // modify old span
//         auto pi = knot_spans[j].min_param_ijk(sd);          // one coordinate of ijk index into params
//         if (mfa.params(mfa.po[sd] + pi) < new_knot)                 // at least one param (domain pt) in the span
//         {
//             while (mfa.params(mfa.po[sd] + pi) < new_knot)          // pi - 1 = max_param_ijk(sd) in the modified span
//                 pi++;
//             knot_spans[j].last_split_dim    = sd;
//             knot_spans[j].max_knot(sd)      = new_knot;
//             knot_spans[j].max_param_ijk(sd) = pi - 1;
//             knot_spans[j].max_param(sd)     = mfa.params(mfa.po[sd] + pi - 1);
// 
//             // modify new span
//             knot_spans.back().last_split_dim     = -1;
//             knot_spans.back().min_knot(sd)       = new_knot;
//             knot_spans.back().min_param_ijk(sd)  = pi;
//             knot_spans.back().min_param(sd)      = mfa.params(mfa.po[sd] + pi);
//             knot_spans.back().min_knot_ijk(sd)++;
// 
//             split_spans[j] = true;
//         }
//     }
// 
//     // increment min and max knot ijk for any knots after the inserted one
//     for (auto j = 0; j < knot_spans.size(); j++)
//     {
//         if (knot_spans[j].min_knot(sd) > knot_spans[si].max_knot(sd))
//             knot_spans[j].min_knot_ijk(sd)++;
//         if (knot_spans[j].max_knot(sd) > knot_spans[si].max_knot(sd))
//             knot_spans[j].max_knot_ijk(sd)++;
//     }
// 
//     // add the new knot to nnew_knots and new_knots
//     new_knots.resize(1);
//     new_knots(0)    = new_knot;
//     nnew_knots      = VectorXi::Zero(mfa.p.size());
//     nnew_knots(sd)  = 1;
// 
//     // debug
// //     cerr << "\n\nAfter splitting spans:" << endl;
// //     for (auto i = 0; i < knot_spans.size(); i++)
// //     {
// //         if (knot_spans[i].done)
// //             continue;
// //         cerr <<
// //             "span_idx="          << i                            <<
// //             "\nmin_knot_ijk:\n"  << knot_spans[i].min_knot_ijk  <<
// //             "\nmax_knot_ijk:\n"  << knot_spans[i].max_knot_ijk  <<
// //             "\nmin_knot:\n"      << knot_spans[i].min_knot      <<
// //             "\nmax_knot:\n"      << knot_spans[i].max_knot      <<
// //             "\nmin_param_ijk:\n" << knot_spans[i].min_param_ijk  <<
// //             "\nmax_param_ijk:\n" << knot_spans[i].max_param_ijk  <<
// //             "\nmin_param:\n"     << knot_spans[i].min_param      <<
// //             "\nmax_param:\n"     << knot_spans[i].max_param      <<
// //             "\n"                 << endl;
// //     }
// }

// NB: The TBB version below is definitely faster (~3X) than the serial. Use the TBB version

#if 1                                   // TBB version

// computes approximated points from a given set of domain points and an n-d NURBS volume
// P&T eq. 9.77, p. 424
// this version recomputes parameter values of input points and
// recomputes basis functions rather than taking them as an input
// this version also assumes weights = 1; no division by weight is done
// assumes all vectors have been correctly resized by the caller
void
mfa::
Decoder::
Decode(MatrixXf& approx)                 // pts in approximated volume (1st dim. changes fastest)
{
    vector<size_t> iter(mfa.p.size(), 0);    // parameter index (iteration count) in current dim.
    vector<size_t> ofst(mfa.p.size(), 0);    // start of current dim in linearized params

    for (size_t i = 0; i < mfa.p.size() - 1; i++)
        ofst[i + 1] = ofst[i] + mfa.ndom_pts(i);

    parallel_for (size_t(0), (size_t)mfa.domain.rows(), [&] (size_t i)
    {
        // convert linear idx to multidim. i,j,k... indices in each domain dimension
        VectorXi ijk(mfa.p.size());
        mfa.idx2ijk(i, ijk);

        // compute parameters for the vertices of the cell
        VectorXf param(mfa.p.size());
        for (int i = 0; i < mfa.p.size(); i++)
            param(i) = mfa.params(ijk(i) + mfa.po[i]);

        // compute approximated point for this parameter vector
        VectorXf cpt(mfa.ctrl_pts.cols());       // approximated point
        VolPt(param, cpt);

        approx.row(i) = cpt;
    });
    fprintf(stderr, "100 %% decoded\n");

    // normal distance computation
    float max_err;                          // max. error found so far
    size_t max_idx;                         // domain point idx at max error
    for (size_t i = 0; i < (size_t)mfa.domain.rows(); i++)
    {
        VectorXf cpt = approx.row(i);
        float err    = fabs(mfa.NormalDistance(cpt, i));
        if (i == 0 || err > max_err)
        {
            max_err = err;
            max_idx = i;
        }
    }

    // normalize max error by size of input data (domain and range)
    float min = mfa.domain.minCoeff();
    float max = mfa.domain.maxCoeff();
    float range = max - min;

    // debug
    fprintf(stderr, "data range = %.1f\n", range);
    fprintf(stderr, "raw max_error = %e\n", max_err);
    cerr << "position of max error: idx=" << max_idx << "\n" << mfa.domain.row(max_idx) << endl;

    max_err /= range;

    fprintf(stderr, "|normalized max_err| = %e\n", max_err);
}

#else                                           // original version

// computes approximated points from a given set of domain points and an n-d NURBS volume
// P&T eq. 9.77, p. 424
// this version recomputes parameter values of input points and
// recomputes basis functions rather than taking them as an input
// this version also assumes weights = 1; no division by weight is done
// assumes all vectors have been correctly resized by the caller
void
mfa::
Decoder::
Decode(MatrixXf& approx)                 // pts in approximated volume (1st dim. changes fastest)
{
    vector<size_t> iter(mfa.p.size(), 0);    // parameter index (iteration count) in current dim.
    vector<size_t> ofst(mfa.p.size(), 0);    // start of current dim in linearized params

    for (size_t i = 0; i < mfa.p.size() - 1; i++)
        ofst[i + 1] = ofst[i] + mfa.ndom_pts(i);

    // eigen frees following temp vectors when leaving scope
    VectorXf dpt(mfa.domain.cols());         // original data point
    VectorXf cpt(mfa.ctrl_pts.cols());       // approximated point
    VectorXf d(mfa.domain.cols());           // apt - dpt
    VectorXf param(mfa.p.size());            // parameters for one point
    for (size_t i = 0; i < mfa.domain.rows(); i++)
    {
        // debug
        // cerr << "input point:\n" << mfa.domain.row(i) << endl;

        // extract parameter vector for one input point from the linearized vector of all params
        for (size_t j = 0; j < mfa.p.size(); j++)
            param(j) = mfa.params(iter[j] + ofst[j]);

        // compute approximated point for this parameter vector
        VolPt(param, cpt);

        // debug
//         cerr << "domain pt:\n" << mfa.domain.row(i) << "\ncpt:\n" << cpt << endl;

        // update the indices in the linearized vector of all params for next input point
        for (size_t j = 0; j < mfa.p.size(); j++)
        {
            if (iter[j] < mfa.ndom_pts(j) - 1)
            {
                iter[j]++;
                break;
            }
            else
                iter[j] = 0;
        }

        approx.row(i) = cpt;

        // print progress
        if (i > 0 && mfa.domain.rows() >= 100 && i % (mfa.domain.rows() / 100) == 0)
            fprintf(stderr, "\r%.0f %% decoded", (float)i / (float)(mfa.domain.rows()) * 100);
    }
    fprintf(stderr, "\r100 %% decoded\n");

    // normal distance computation
    float max_err;                          // max. error found so far
    size_t max_idx;                         // domain point idx at max error
    for (size_t i = 0; i < (size_t)mfa.domain.rows(); i++)
    {
        VectorXf cpt = approx.row(i);
        float err    = fabs(mfa.NormalDistance(cpt, i));
        if (i == 0 || err > max_err)
        {
            max_err = err;
            max_idx = i;
        }
    }

    // normalize max error by size of input data (domain and range)
    float min = mfa.domain.minCoeff();
    float max = mfa.domain.maxCoeff();
    float range = max - min;

    // debug
    fprintf(stderr, "data range = %.1f\n", range);
    fprintf(stderr, "raw max_error = %e\n", max_err);
    cerr << "position of max error: idx=" << max_idx << "\n" << mfa.domain.row(max_idx) << endl;

    max_err /= range;

    fprintf(stderr, "|normalized max_err| = %e\n", max_err);
}

#endif

// DEPRECATED
// // compute a point from a NURBS curve at a given parameter value
// // algorithm 4.1, Piegl & Tiller (P&T) p.124
// // this version recomputes basis functions rather than taking them as an input
// // this version also assumes weights = 1; no division by weight is done
// void
// mfa::
// Decoder::
// CurvePt(int       cur_dim,                     // current dimension
//         float     param,                       // parameter value of desired point
//         VectorXf& out_pt,                      // (output) point
//         int       ko)                          // starting knot offset (default = 0)
// {
//     int n      = (int)mfa.ctrl_pts.rows() - 1;     // number of control point spans
//     int span   = mfa.FindSpan(cur_dim, param, ko);
//     MatrixXf N = MatrixXf::Zero(1, n + 1);     // basis coefficients
//     mfa.BasisFuns(cur_dim, param, span, N, 0, n, 0, ko);
//     out_pt = VectorXf::Zero(mfa.ctrl_pts.cols());  // initializes and resizes
// 
//     for (int j = 0; j <= mfa.p(cur_dim); j++)
//         out_pt += N(0, j + span - mfa.p(cur_dim)) * mfa.ctrl_pts.row(span - mfa.p(cur_dim) + j);
// 
//     // clamp dimensions other than cur_dim to same value as first control point
//     // eliminates any wiggles in other dimensions due to numerical precision errors
//     for (auto j = 0; j < mfa.p.size(); j++)
//         if (j != cur_dim)
//             out_pt(j) = mfa.ctrl_pts(span - mfa.p(cur_dim), j);
//     // debug
//     // cerr << "n " << n << " param " << param << " span " << span << " out_pt " << out_pt << endl;
//     // cerr << " N " << N << endl;
// }

// compute a point from a NURBS curve at a given parameter value
// this version takes a temporary set of control points for one curve only rather than
// reading full n-d set of control points from the mfa
// algorithm 4.1, Piegl & Tiller (P&T) p.124
// this version recomputes basis functions rather than taking them as an input
// this version also assumes weights = 1; no division by weight is done
void
mfa::
Decoder::
CurvePt(
        int       cur_dim,                     // current dimension
        float     param,                       // parameter value of desired point
        MatrixXf& temp_ctrl,                   // temporary control points
        VectorXf& out_pt,                      // (output) point
        int       ko)                          // starting knot offset (default = 0)
{
    int n      = (int)temp_ctrl.rows() - 1;     // number of control point spans
    // span is computed from n-d set of knots (needs ko offset) but then converted to 1-d curve span
    // by subtracting ko
    int span   = mfa.FindSpan(cur_dim, param, ko) - ko;
    MatrixXf N = MatrixXf::Zero(1, n + 1);     // basis coefficients
    mfa.BasisFuns(cur_dim, param, span, N, 0, n, 0);
    out_pt = VectorXf::Zero(temp_ctrl.cols());  // initializes and resizes

    for (int j = 0; j <= mfa.p(cur_dim); j++)
        out_pt += N(0, j + span - mfa.p(cur_dim)) * temp_ctrl.row(span - mfa.p(cur_dim) + j);

    // clamp dimensions other than cur_dim to same value as first control point
    // eliminates any wiggles in other dimensions due to numerical precision errors
    for (auto j = 0; j < mfa.p.size(); j++)
        if (j != cur_dim)
            out_pt(j) = temp_ctrl(0, j);

    // debug
//     cerr << " param " << param << " span " << span << " out_pt:\n" << out_pt << endl;
//     cerr << "param " << param << " span " << span << " ko " << ko << endl;
}

// compute a point from a NURBS n-d volume at a given parameter value
// algorithm 4.3, Piegl & Tiller (P&T) p.134
// this version recomputes basis functions rather than taking them as an input
// this version also assumes weights = 1; no division by weight is done
//
// There are two types of dimensionality:
// 1. The dimensionality of the NURBS tensor product (p.size())
// (1D = NURBS curve, 2D = surface, 3D = volumem 4D = hypervolume, etc.)
// 2. The dimensionality of individual control points (ctrl_pts.cols())
// p.size() should be < ctrl_pts.cols()
void
mfa::
Decoder::
VolPt(VectorXf& param,                       // parameter value in each dim. of desired point
      VectorXf& out_pt)                      // (output) point
{
    // check dimensionality for sanity
    assert(mfa.p.size() < mfa.ctrl_pts.cols());

    out_pt = VectorXf::Zero(mfa.ctrl_pts.cols());   // initializes and resizes
    vector <MatrixXf> N(mfa.p.size());              // basis functions in each dim.
    vector<VectorXf>  temp(mfa.p.size());           // temporary point in each dim.
    vector<int>       span(mfa.p.size());           // span in each dim.
    vector<int>       n(mfa.p.size());              // number of control point spans in each dim
    vector<int>       iter(mfa.p.size());           // iteration number in each dim.
    VectorXf          ctrl_pt(mfa.ctrl_pts.cols()); // one control point
    vector<size_t>    co(mfa.p.size());             // starting ofst for ctrl pts in each dim for this span
    int ctrl_idx;                               // control point linear ordering index

    // init
    for (size_t i = 0; i < mfa.p.size(); i++)       // for all dims
    {
        temp[i]    = VectorXf::Zero(mfa.ctrl_pts.cols());
        iter[i]    = 0;
        n[i]       = (int)mfa.nctrl_pts(i) - 1;
        span[i]    = mfa.FindSpan(i, param(i), mfa.ko[i]);
        N[i]       = MatrixXf::Zero(1, n[i] + 1);
        co[i]      = span[i] - mfa.p(i) - mfa.ko[i];
        mfa.BasisFuns(i, param(i), span[i], N[i], 0, n[i], 0, mfa.ko[i]);
    }

    for (int i = 0; i < tot_iters; i++)      // 1-d flattening all n-d nested loop computations
    {
        // control point linear order index
        ctrl_idx = 0;
        for (int j = 0; j < mfa.p.size(); j++)
            ctrl_idx += (co[j] + ct(i, j)) * cs[j];

        // always compute the point in the first dimension
        ctrl_pt =  mfa.ctrl_pts.row(ctrl_idx);
        temp[0] += (N[0])(0, iter[0] + span[0] - mfa.p(0)) * ctrl_pt;
        iter[0]++;

        // for all dimensions except last, check if span is finished
        for (size_t k = 0; k < mfa.p.size() - 1; k++)
        {
            if (iter[k] - 1 == mfa.p(k))
            {
                // compute point in next higher dimension and reset computation for current dim
                temp[k + 1] += (N[k + 1])(0, iter[k + 1] + span[k + 1] - mfa.ko[k + 1] - mfa.p(k + 1)) * temp[k];
                temp[k]     =  VectorXf::Zero(mfa.ctrl_pts.cols());
                iter[k]     =  0;
                iter[k + 1]++;
            }
        }
    }

    out_pt = temp[mfa.p.size() - 1];

    // debug
    // fprintf(stderr, "out_pt = temp[%d]\n", mfa.p.size() - 1);
    // cerr << "out_pt:\n" << out_pt << endl;
}
