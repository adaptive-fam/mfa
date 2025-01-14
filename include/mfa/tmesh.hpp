//--------------------------------------------------------------
// T-mesh object
//
// Tom Peterka
// Argonne National Laboratory
// tpeterka@mcs.anl.gov
//--------------------------------------------------------------
#ifndef _TMESH_HPP
#define _TMESH_HPP

using namespace std;

using KnotIdx   = size_t;
using TensorIdx = size_t;
using ParamIdx  = size_t;

struct NeighborTensor                                   // neighboring tensor product
{
    int         dim;                                    // neighbor is in this dimension from the original tensor
    int         level;                                  // level of neighbor
    TensorIdx   tensor_idx;                             // index in tensor_prods of the neighbor
};

template <typename T>
struct TensorProduct
{
    vector<KnotIdx>             knot_mins;              // indices into all_knots
    vector<KnotIdx>             knot_maxs;              // indices into all_knots
    VectorXi                    nctrl_pts;              // number of control points in each domain dimension
    MatrixX<T>                  ctrl_pts;               // control points in row major order
    VectorX<T>                  weights;                // weights associated with control points
    vector<vector<TensorIdx>>   next;                   // next[dim][index of next tensor product] (unsorted)
    vector<vector<TensorIdx>>   prev;                   // prev[dim][index of previous tensor product] (unsorted)
    int                         level;                  // refinement level
    vector<vector<KnotIdx>>     knot_idxs;              // all_knots indices of knots belonging to this tensor [dim][index] (sorted)
    bool                        done;                   // no more knots need to be added to this tensor

    // following is used for candidate tensors during refinement only and otherwise not maintained
    // TODO: if deemed useful for other purposes, set and maintain for all tensors
    TensorIdx                   parent;                 // parent from which this tensor was refined
    bool                        parent_exists;          // parent exists and the index to it is valid

    TensorProduct() : done(false)   {}
    TensorProduct(int dom_dim) : done(false)
    {
        knot_mins.resize(dom_dim);
        knot_maxs.resize(dom_dim);
        nctrl_pts.resize(dom_dim);
        next.resize(dom_dim);
        prev.resize(dom_dim);
        knot_idxs.resize(dom_dim);
    }
    TensorProduct(vector<KnotIdx>& knot_mins_, vector<KnotIdx>& knot_maxs_) :
        done(false),
        knot_mins(knot_mins_),
        knot_maxs(knot_maxs_)
    {
        int dom_dim = knot_mins.size();
        nctrl_pts.resize(dom_dim);
        next.resize(dom_dim);
        prev.resize(dom_dim);
        knot_idxs.resize(dom_dim);
    }
};

namespace mfa
{
    template <typename T>
    struct Tmesh
    {
        vector<vector<T>>           all_knots;          // all_knots[dimension][index] (sorted)
        vector<vector<int>>         all_knot_levels;    // refinement levels of all_knots[dimension][index]
        vector<vector<ParamIdx>>    all_knot_param_idxs;// index of first input point whose parameter is >= knot value in all_knots[dimension][idx] (same layout as all_knots)
                                                        // knot value <= params[dim][idx] < next knot value
        vector<TensorProduct<T>>    tensor_prods;       // all tensor products
        int                         dom_dim_;           // domain dimensionality
        VectorXi                    p_;                 // degree in each dimension
        int                         min_dim_;           // starting coordinate of this model in full-dimensional data
        int                         max_dim_;           // ending coordinate of this model in full-dimensional data
        int                         cur_split_dim;      // current split dimension
        int                         max_level;          // deepest level of refinement

        Tmesh(int               dom_dim,                // number of domain dimension
              const VectorXi&   p,                      // degree in each dimension
              int               min_dim,                // starting coordinate of this model in full-dimensional data
              int               max_dim,                // ending coordinate of this model in full-dimensional data
              size_t            ntensor_prods =  0) :   // number of tensor products to allocate
                dom_dim_(dom_dim),
                p_(p),
                min_dim_(min_dim),
                max_dim_(max_dim),
                cur_split_dim(0),
                max_level(0)
        {
            all_knots.resize(dom_dim_);
            all_knot_levels.resize(dom_dim_);
            all_knot_param_idxs.resize(dom_dim_);

            if (ntensor_prods)
                tensor_prods.resize(ntensor_prods);
        }

        // initialize knots
        void init_knots(VectorXi& nctrl_pts)
        {
            for (auto i = 0; i < dom_dim_; i++)
            {
                all_knots[i].resize(nctrl_pts(i) + p_(i) + 1);
                all_knot_levels[i].resize(nctrl_pts(i) + p_(i) + 1);
                all_knot_param_idxs[i].resize(nctrl_pts(i) + p_(i) + 1);
            }
        }

        // checks if a knot can be inserted in a given position
        bool can_insert_knot(int        dim,                // current dimension
                             KnotIdx    pos,                // position
                             T          knot)               // knot value
        {
            // checks that the knot is not a duplicate and is not in the wrong position
            if ( (all_knots[dim][pos] == knot)                  ||
                (pos > 0 && all_knots[dim][pos - 1] >= knot)    ||
                (pos < all_knots[dim].size() - 1 && all_knots[dim][pos + 1] <= knot) )
                return false;
            return true;
        }

        // insert a knot into all_knots at an unknown position
        // checks for duplicates and invalid insertions
        // returns:
        // 0: no change in knots or levels
        // 1: changed level of an existing knot
        // 2: inserted a new knot and level
        int insert_knot(
                int                        dim,                 // current dimension
                int                        level,               // refinement level of inserted knot
                T                          knot,                // knot value to be inserted
                const vector<vector<T>>&   params,              // params of input points
                KnotIdx&                   pos)                 // (output) inserted position
        {
            pos = FindSpan(dim, knot);
            if (knot > all_knots[dim][pos])
                pos++;
            if (level > max_level)
                max_level = level;
            return insert_knot_at_pos(dim, pos, level, knot, params);
        }

        // insert a knot into all_knots at a given position
        // checks for duplicates and invalid insertions
        // adjusts all tensor products knot_mins, knot_maxs accordingly
        // returns:
        // 0: no change in knots or levels
        // 1: changed level of an existing knot
        // 2: inserted a new knot and level
        int insert_knot_at_pos(
                int                        dim,                 // current dimension
                KnotIdx                    pos,                 // new position in all_knots[dim] of inserted knot
                int                        level,               // refinement level of inserted knot
                T                          knot,                // knot value to be inserted
                const vector<vector<T>>&   params)              // params of input points
        {
            // if knot exists already, just update its level
            if (all_knots[dim][pos] == knot)
            {
                // update to highest (most coarse) level
                if (level < all_knot_levels[dim][pos])
                {
                    all_knot_levels[dim][pos] = level;
                    return 1;
                }
                else
                    return 0;
            }

            // check if knot is out of order
            if ( (pos > 0 && all_knots[dim][pos - 1] >= knot) ||
                (pos < all_knots[dim].size() - 1 && all_knots[dim][pos + 1] <= knot) )
            {
                fmt::print(stderr, "Error: insert_knot(): attempting to insert a knot out of order\n");
                fmt::print(stderr, "dim {} pos {} knot {} level {}\n", dim, pos, knot, level);
                print_knots();
                abort();
            }

            // insert knot and level
            all_knots[dim].insert(all_knots[dim].begin() + pos, knot);
            all_knot_levels[dim].insert(all_knot_levels[dim].begin() + pos, level);

            // insert param idx
            auto        param_it = params[dim].begin();     // iterator into params (for one dim.)
            // uninitialized values, search entire params
            if (pos > 0 && all_knot_param_idxs[dim][pos - 1] == all_knot_param_idxs[dim][pos])
                param_it = lower_bound(params[dim].begin(), params[dim].end(), all_knots[dim][pos]);
            else if (pos > 0)       // search for the param idx within the bounds of existing knot values
            {
                ParamIdx low    = all_knot_param_idxs[dim][pos - 1];
                ParamIdx high   = all_knot_param_idxs[dim][pos];
                param_it        = lower_bound(params[dim].begin() + low, params[dim].begin() + high, all_knots[dim][pos]);
            }

            ParamIdx param_idx = param_it - params[dim].begin();
            all_knot_param_idxs[dim].insert(all_knot_param_idxs[dim].begin() + pos, param_idx);

            // adjust tensor product knot_mins and knot_maxs and local knot indices
            for (auto& t : tensor_prods)
            {
                if (t.knot_mins[dim] >= pos)
                    t.knot_mins[dim]++;
                if (t.knot_maxs[dim] >= pos)
                    t.knot_maxs[dim]++;
                tensor_knot_idxs(t);
            }

            return 2;
        }

        // append a tensor product to the vector of tensor_prods
        // if a tensor matching the knot mins and maxs exists, resizes its control points to the current number of knots
        // returns index of new or existing tensor in the vector of tensor products
        int append_tensor(const vector<KnotIdx>&   knot_mins,       // indices in all_knots of min. corner of tensor to be inserted
                          const vector<KnotIdx>&   knot_maxs,       // indices in all_knots of max. corner
                          int                      level = -1,      // optional level to assign to new tensor, -1 = choose next level automatically
                          bool                     debug = false)   // print debugging output
        {
            bool vec_grew;                          // vector of tensor_prods grew
            bool tensor_inserted = false;           // the desired tensor was already inserted

            // check if the tensor to be added matches any existing ones
            for (auto k = 0; k < tensor_prods.size(); k++)
            {
                auto& t = tensor_prods[k];

                if (knot_mins == t.knot_mins && knot_maxs == t.knot_maxs)
                {
                    // adjust level
                    if (level >= 0)
                        t.level = level;

                    // resize control points and weights
                    for (auto j = 0; j < dom_dim_; j++)
                    {
                        bool odd_degree = p_(j) % 2;
                        t.nctrl_pts(j) = knot_idx_dist(t, knot_mins[j], knot_maxs[j], j, odd_degree);
                        if (knot_mins[j] == 0)
                            t.nctrl_pts(j) -= (p_(j) + 1) / 2;
                        if (knot_maxs[j] == all_knots[j].size() - 1)
                            t.nctrl_pts(j) -= (p_(j) + 1) / 2;
                    }
                    t.ctrl_pts.resize(t.nctrl_pts.prod(), t.ctrl_pts.cols());
                    t.weights = VectorX<T>::Ones(t.ctrl_pts.rows());
                    tensor_knot_idxs(t);

                    return k;
                }
            }

            // create a new tensor product
            TensorProduct<T> new_tensor(dom_dim_);
            new_tensor.knot_mins = knot_mins;
            new_tensor.knot_maxs = knot_maxs;

            // initialize control points
            new_tensor.nctrl_pts.resize(dom_dim_);
            size_t tot_nctrl_pts = 1;
            if (!tensor_prods.size())
            {
                new_tensor.level = 0;
                tensor_knot_idxs(new_tensor);

                // resize control points
                // level 0 has only one box of control points
                for (auto j = 0; j < dom_dim_; j++)
                {
                    new_tensor.nctrl_pts[j] = all_knots[j].size() - p_(j) - 1;
                    tot_nctrl_pts *= new_tensor.nctrl_pts[j];
                }
                new_tensor.ctrl_pts.resize(tot_nctrl_pts, max_dim_ - min_dim_ + 1);
                new_tensor.weights.resize(tot_nctrl_pts);               // will get initialized to 1 later
            }
            else
            {
                new_tensor.level = (level == -1 ? tensor_prods.back().level + 1 : level);
                tensor_knot_idxs(new_tensor);

                // resize control points
                for (auto j = 0; j < dom_dim_; j++)
                {
                    // count number of knots in the new tensor in this dimension
                    // inserted tensor is at the deepest level of refinement, ie, all knots in the global knot vector between
                    // min and max knots are in this tensor (don't skip any knots)
                    size_t nknots   = 0;
                    size_t nanchors = 0;
                    for (auto i = knot_mins[j]; i <= knot_maxs[j]; i++)
                        nknots++;
                    if (p_(j) % 2 == 0)         // even degree: anchors are between knot lines
                        nanchors = nknots - 1;
                    else                            // odd degree: anchors are on knot lines
                        nanchors = nknots;
                    if (knot_mins[j] < p_(j) - 1)                       // skip up to p-1 anchors at start of global knots
                        nanchors -= (p_(j) - 1 - knot_mins[j]);
                    if (knot_maxs[j] > all_knots[j].size() - p_(j))     // skip up to p-1 anchors at end of global knots
                        nanchors -= (knot_maxs[j] + p_(j) - all_knots[j].size());
                    new_tensor.nctrl_pts[j] = nanchors;
                    tot_nctrl_pts *= nanchors;
                }
                new_tensor.ctrl_pts.resize(tot_nctrl_pts, max_dim_ - min_dim_ + 1);
                new_tensor.weights.resize(tot_nctrl_pts);               // will get initialized to 1 later
            }

//             if (debug)
//                 fmt::print("append_tensor(): 1: knot_mins [{}] knot_maxs [{}] tot_nctrl_pts {}\n",
//                         fmt::join(knot_mins, ","), fmt::join(knot_maxs, ","), tot_nctrl_pts);

            vector<int> split_side(dom_dim_);       // whether min (-1), max (1), or both (2) sides of
                                                    // new tensor split the existing tensor (one value for each dim.)

            // check for intersection of the new tensor with existing tensors
            do
            {
                vec_grew = false;           // tensor_prods grew and iterator is invalid
                bool knots_match;           // intersect resulted in a tensor with same knot mins, maxs as tensor to be added

                for (auto j = 0; j < tensor_prods.size(); j++)      // for all tensor products
                {
//                     if (debug)
//                         fmt::print(stderr, "checking for intersection between new tensor and existing tensor idx={}\n", j);

                    // check if new tensor completely covers existing tensor, if so, delete existing
                    if (subset(tensor_prods[j].knot_mins, tensor_prods[j].knot_maxs, new_tensor.knot_mins, new_tensor.knot_maxs))
                    {
//                         if (debug)
//                             fmt::print("append_tensor(): new tensor covers existing tensor {} which will be deleted\n", j);

                        // delete neighbors' prev/next pointers to this tensor
                        for (auto i = 0; i < dom_dim_; i++)
                        {
                            auto& prev = tensor_prods[j].prev[i];
                            for (auto k = 0; k < prev.size(); k++)
                                delete_pointer(prev[k], j);
                            auto& next = tensor_prods[j].next[i];
                            for (auto k = 0; k < next.size(); k++)
                                delete_pointer(next[k], j);
                        }

                        // delete the tensor by swapping the last tensor in its place
                        // TODO: deep copy can be avoided by marking tensor as dirty and reclaiming on next append
                        tensor_prods[j] = tensor_prods.back();
                        tensor_prods.resize(tensor_prods.size() - 1);

                        // renumber the pointers of the moved tensor's neighbors
                        for (auto i = 0; i < dom_dim_; i++)
                        {
                            auto& prev = tensor_prods[j].prev[i];
                            for (auto k = 0; k < prev.size(); k++)
                                change_pointer(prev[k], tensor_prods.size(), j);
                            auto& next = tensor_prods[j].next[i];
                            for (auto k = 0; k < next.size(); k++)
                                change_pointer(next[k], tensor_prods.size(), j);
                        }

//                         if (debug)
//                         {
//                             fmt::print("\nappend_tensor(): tensors after deletion\n\n");
//                             print_tensors();
//                         }
                    }

                    if (nonempty_intersection(new_tensor, tensor_prods[j], split_side))
                    {
//                         if (debug)
//                         {
//                             fmt::print("append_tensor(): intersection found between new tensor and existing tensor idx={} split_side=[{}]\n",
//                                     j, fmt::join(split_side, ","));
//                             fmt::print("\nappend_tensor(): tensors before intersection\n\n");
//                             print(false, true, false, false);
//                         }

                        if ((vec_grew = intersect(new_tensor, j, split_side, knots_match, debug)) && vec_grew)
                        {
                            if (knots_match)
                                tensor_inserted = true;

//                             if (debug)
//                             {
//                                 fmt::print("\nappend_tensor(): tensors after intersection\n\n");
//                                 print(false, true, false, false);
//                             }

                            break;  // adding a tensor invalidates iterator, start iteration over
                        }   // vec_grew
                    }   // nonempty_intersection
                }   // for all tensor products
            } while (vec_grew);   // keep checking until no more tensors are added

            // the new tensor has either been added already or still needs to be added
            // either way, create reference to new tensor and get its index at the end of vector of tensor prods.
            TensorIdx           new_tensor_idx;
            TensorProduct<T>&   new_tensor_ref = new_tensor;
            if (!tensor_inserted)
            {
                // new tensor will go at the back of the vector
                new_tensor_idx = tensor_prods.size();
            }
            else
            {
                // new tensor is already at the back of the vector
                new_tensor_idx = tensor_prods.size() - 1;
                new_tensor_ref = tensor_prods[new_tensor_idx];
            }

            // adjust next and prev pointers for new tensor
            for (int j = 0; j < dom_dim_; j++)
            {
                for (auto k = 0; k < new_tensor_idx; k++)
                {
//                     if (debug)
//                         fprintf(stderr, "final add: cur_dim=%d new_tensor_idx=%lu checking existing_tensor_idx=%lu\n", j, new_tensor_idx, k);

                    TensorProduct<T>& existing_tensor_ref = tensor_prods[k];
                    int adjacent_retval = adjacent(new_tensor_ref, existing_tensor_ref, j);

                    if (adjacent_retval == 1)
                    {
                        new_tensor_ref.next[j].push_back(k);
                        existing_tensor_ref.prev[j].push_back(new_tensor_idx);
                    }
                    else if (adjacent_retval == -1)
                    {
                        new_tensor_ref.prev[j].push_back(k);
                        existing_tensor_ref.next[j].push_back(new_tensor_idx);
                    }
                }
            }

            // initialize the new tensor weights to 1
            // done here, relatively late, because it's possible for it to have changed, eg, set to MFA_NAW earlier
            // this is how we ensure the new tensor has all valid weights
            new_tensor.weights = VectorX<T>::Ones(new_tensor.weights.size());

            // add the tensor
            if (!tensor_inserted)
                tensor_prods.push_back(new_tensor);

            return new_tensor_idx;
        }

        // Append a tensor product to the back of tensor_prods and copy control points and weights into it
        // This version takes a (possibly larger) set of control points and weights to copy into the appended tensor
        // If the input control points and weights are a superset of the tensor, the correct subset of them will be used
        // The number of control points is the number of the input superset
        void append_tensor(const vector<KnotIdx>&   knot_mins,          // indices in all_knots of min. corner of tensor to be inserted
                           const vector<KnotIdx>&   knot_maxs,          // indices in all_knots of max. corner
                           const VectorXi&          new_nctrl_pts,      // number of control points in each dim. for this tensor (possibly superset)
                           const MatrixX<T>&        new_ctrl_pts,       // local control points for this tensor (possibly superset)
                           const VectorX<T>&        new_weights,        // local weights for this tensor (possibly superset)
                           int                      parent_tensor_idx)  // idx of parent tensor from which the superset of control points derives
        {
            bool vec_grew;                          // vector of tensor_prods grew
            bool tensor_inserted = false;           // the desired tensor was already inserted

            // create a new tensor product
            TensorProduct<T> new_tensor(dom_dim_);
            new_tensor.knot_mins = knot_mins;
            new_tensor.knot_maxs = knot_maxs;

            // initialize control points
            new_tensor.nctrl_pts.resize(dom_dim_);
            if (!tensor_prods.size())
                new_tensor.level = 0;
            else
                new_tensor.level = tensor_prods.back().level + 1;

            tensor_knot_idxs(new_tensor);

            vector<int> split_side(dom_dim_);       // whether min (-1) or max (1) or both (2) sides of
                                                    // new tensor are inside existing tensor (one value for each dim.)

            // check for intersection of the new tensor with existing tensors
            do
            {
                vec_grew = false;           // tensor_prods grew and iterator is invalid
                bool knots_match;           // intersect resulted in a tensor with same knot mins, maxs as tensor to be added

                for (auto j = 0; j < tensor_prods.size(); j++)
                {
                    // debug
//                     fprintf(stderr, "checking for intersection between new tensor and existing tensor idx=%lu\n", j);

                    if (nonempty_intersection(new_tensor, tensor_prods[j], split_side))
                    {
                        // debug
//                         fprintf(stderr, "intersection found between new tensor and existing tensor idx=%d split_side=[%d %d]\n",
//                                 j, split_side[0], split_side[1]);
//                         fprintf(stderr, "\ntensors before intersection\n\n");
//                         print();

                        if ((vec_grew = intersect(new_tensor, j, split_side, knots_match)) && vec_grew)
                        {
                            if (knots_match)
                                tensor_inserted = true;

                            // debug
//                             fprintf(stderr, "\ntensors after intersection\n\n");
//                             print();

                            break;  // adding a tensor invalidates iterator, start iteration over
                        }
                    }
                }
            } while (vec_grew);   // keep checking until no more tensors are added

            // the new tensor has either been added already or still needs to be added
            // either way, create reference to new tensor and get its index at the end of vector of tensor prods.
            TensorIdx           new_tensor_idx;
            TensorProduct<T>&   new_tensor_ref = new_tensor;
            if (!tensor_inserted)
            {
                // new tensor will go at the back of the vector
                new_tensor_idx = tensor_prods.size();
                new_tensor_ref = new_tensor;
            }
            else
            {
                // new tensor is already at the back of the vector
                new_tensor_idx = tensor_prods.size() - 1;
                new_tensor_ref = tensor_prods[new_tensor_idx];
            }

            // adjust next and prev pointers for new tensor
            for (int j = 0; j < dom_dim_; j++)
            {
                for (auto k = 0; k < new_tensor_idx; k++)
                {
                    // debug
//                     fprintf(stderr, "final add: cur_dim=%d new_tensor_idx=%lu checking existing_tensor_idx=%lu\n", j, new_tensor_idx, k);

                    TensorProduct<T>& existing_tensor_ref = tensor_prods[k];
                    int adjacent_retval = adjacent(new_tensor_ref, existing_tensor_ref, j);

                    if (adjacent_retval == 1)
                    {
                        new_tensor_ref.next[j].push_back(k);
                        existing_tensor_ref.prev[j].push_back(new_tensor_idx);
                    }
                    else if (adjacent_retval == -1)
                    {
                        new_tensor_ref.prev[j].push_back(k);
                        existing_tensor_ref.next[j].push_back(new_tensor_idx);
                    }
                }
            }

            // add the tensor
            if (!tensor_inserted)
                tensor_prods.push_back(new_tensor);

            // copy the control points
            // TODO: deal with the case that the tensor was already inserted, check if it's possible to not be at the end
            // following assumes the appended tensor is last
            int tensor_idx = tensor_prods.size() - 1;
            subset_ctrl_pts(new_nctrl_pts, new_ctrl_pts, new_weights, tensor_idx, parent_tensor_idx);
        }

        // check if nonempty intersection exists in all dimensions between knot_mins, knot_maxs of two tensors
        bool nonempty_intersection(TensorProduct<T>&    new_tensor,         // new tensor product to be added
                                   TensorProduct<T>&    existing_tensor,    // existing tensor product
                                   vector<int>&         split_side)         // (output) whether none (0), min (-1), max (1), or
                                                                            // both (2) sides of new_tensor split
                                                                            // existing tensor (one value for each dim.)
        {
            split_side.clear();
            split_side.resize(dom_dim_);
            bool retval = false;
            for (int j = 0; j < dom_dim_; j++)
            {
                if (new_tensor.knot_mins[j] > existing_tensor.knot_mins[j] && new_tensor.knot_mins[j] < existing_tensor.knot_maxs[j])
                {
//                     // debug
//                     fprintf(stderr, "cur_dim=%d split_side=-1 new min %lu exist min %lu exist max %lu\n",
//                             j, new_tensor.knot_mins[j], existing_tensor.knot_mins[j], existing_tensor.knot_maxs[j]);

                    split_side[j] = -1;
                    retval = true;
                }
                if (new_tensor.knot_maxs[j] > existing_tensor.knot_mins[j] && new_tensor.knot_maxs[j] < existing_tensor.knot_maxs[j])
                {
                    // debug
//                     fprintf(stderr, "cur_dim=%d split_side=1 new max %lu exist min %lu exist max %lu\n",
//                             j, new_tensor.knot_maxs[j], existing_tensor.knot_mins[j], existing_tensor.knot_maxs[j]);

                    if (!split_side[j])
                        split_side[j] = 1;
                    else
                        split_side[j] = 2;
                    retval = true;
                }
                // if no intersection found in this dimension, in order to continue checking other dimensions,
                // new_tensor must match exactly or be bigger than existing_tensor. Otherwise, no intersection exists.
                if ( !split_side[j] &&
                     (new_tensor.knot_mins[j] > existing_tensor.knot_mins[j] || new_tensor.knot_maxs[j] < existing_tensor.knot_maxs[j]) )
                    return false;
            }

            return retval;
        }

        // checks if two tensors intersect to within a padding distance
        // adjacency (to within pad distance) counts as an intersection if adjacency_counts = true (default)
        // subset also counts as intersection
        bool intersect(TensorProduct<T>&    t1,
                       TensorProduct<T>&    t2,
                       KnotIdx              pad = 0,
                       bool                 adjacency_counts = true)
        {
            for (auto k = 0; k < dom_dim_; k++)
            {
                if (adjacency_counts)
                {
                    if (t1.knot_maxs[k] + pad < t2.knot_mins[k] || t2.knot_maxs[k] + pad < t1.knot_mins[k])
                        return false;
                }
                if (!adjacency_counts)
                {
                    if (t1.knot_maxs[k] + pad <= t2.knot_mins[k] || t2.knot_maxs[k] + pad <= t1.knot_mins[k])
                        return false;
                }
            }
            return true;
        }

        // intersect a new tensor product with an existing tensor product, if the intersection exists
        // returns true if intersection found (and the vector of tensor products grew as a result of the intersection, ie, an existing tensor was split into two)
        // sets knots_match to true if during the course of intersecting, one of the tensors in tensor_prods was added or modified to match the new tensor
        // ie, the caller should not add the tensor later if knots_match
        bool intersect(TensorProduct<T>&    new_tensor,             // new tensor product to be inserted
                       TensorIdx            existing_tensor_idx,    // index in tensor_prods of existing tensor
                       vector<int>&         split_side,             // whether min (-1), max (1), or both sides (2) of new tensor
                                                                    // split the existing tensor (one value for each dim.)
                       bool&                knots_match,            // (output) interection resulted in a tensor whose knot mins, max match new tensor's
                       bool                 debug = false)          // print debug output
        {
            knots_match             = false;
            bool    retval          = false;
            KnotIdx split_knot_idx;

            for (int i = 0; i < dom_dim_; i++)      // for all domain dimensions
            {
                int k = next_split_dim();           // alternate splitting dimension

                // debug
//                 fmt::print("intersect(): i = {} k = {}\n", i, k);

                if (!split_side[k])
                    continue;

                split_knot_idx                      = (split_side[k] == -1 ? new_tensor.knot_mins[k] : new_tensor.knot_maxs[k]);
                TensorProduct<T>&   existing_tensor = tensor_prods[existing_tensor_idx];
                vector<KnotIdx>     temp_maxs       = existing_tensor.knot_maxs;
                vector<KnotIdx>     temp_mins       = existing_tensor.knot_mins;
                if (split_side[k] == -1)
                    temp_maxs[k] = split_knot_idx;
                else
                    temp_mins[k] = split_knot_idx;

                // split existing_tensor at the knot index knot_idx as long as doing so would not create
                // a tensor that is a subset of new_tensor being inserted
                // existing_tensor is modified to be the min. side of the previous existing_tensor
                // a new max_side_tensor is appended to be the max. side of existing_tensor
                if (!subset(temp_mins, temp_maxs, new_tensor.knot_mins, new_tensor.knot_maxs))
                {
                    // if there is a new tensor, return and start checking again for intersections
                    retval |= new_side(new_tensor, existing_tensor_idx, k, split_knot_idx, split_side[k], knots_match, debug);
                    if (retval)
                        return true;
                }
            }
            return retval;
        }

        // checks if a_mins, maxs intersect b_mins, maxs, with the intersection in c_mins, c_maxs
        // returns whether there is an intersection (larger than edges just touching)
        bool intersects(const vector<KnotIdx>&  a_mins,
                        const vector<KnotIdx>&  a_maxs,
                        const vector<KnotIdx>&  b_mins,
                        const vector<KnotIdx>&  b_maxs,
                        vector<KnotIdx>&        c_mins,
                        vector<KnotIdx>&        c_maxs) const
        {
            // check that sizes are identical
            size_t a_size = a_mins.size();
            if (a_size != a_maxs.size() || a_size != b_mins.size() || a_size != b_maxs.size() ||
                    a_size != c_mins.size() || a_size != c_maxs.size())
            {
                fprintf(stderr, "Error: intersects(): size mismatch\n");
                abort();
            }

            // check intersection cases
            for (auto i = 0; i < a_size; i++)
            {
                // no intersection
                if (a_maxs[i] <= b_mins[i] || b_maxs[i] <= a_mins[i])
                        return false;

                // a is a subset of b
                else if (a_mins[i] >= b_mins[i] && a_maxs[i] <= b_maxs[i])
                {
                    c_mins[i] = a_mins[i];
                    c_maxs[i] = a_maxs[i];
                }

                // b is a subset of a
                else if (b_mins[i] >= a_mins[i] && b_maxs[i] <= a_maxs[i])
                {
                    c_mins[i] = b_mins[i];
                    c_maxs[i] = b_maxs[i];
                }

                // a is to the left of b but overlaps it
                else if (a_maxs[i] > b_mins[i] && a_maxs[i] < b_maxs[i])
                {
                    c_mins[i] = b_mins[i];
                    c_maxs[i] = a_maxs[i];
                }

                // b is to the left of a but overlaps it
                else if (b_maxs[i] > a_mins[i] && b_maxs[i] < a_maxs[i])
                {
                    c_mins[i] = a_mins[i];
                    c_maxs[i] = b_maxs[i];
                }

                else
                {
                    fprintf(stderr, "Error: intersects(): ran out of cases\n");
                    abort();
                }
            }

            // debug
//             fmt::print(stderr, "intersects(): min [{}] : max [{}] intersects min [{}] : max[{}] with intersection min [{}] : max [{}]\n",
//                     fmt::join(a_mins, ","), fmt::join(a_maxs, ","), fmt::join(b_mins, ","), fmt::join(b_maxs, ","),
//                     fmt::join(c_mins, ","), fmt::join(c_maxs, ","));

            return true;
        }

        // return the next dimension to split a tensor
        int next_split_dim()
        {
            int retval      = cur_split_dim;
            cur_split_dim   = (cur_split_dim + 1) % dom_dim_;
            return retval;
        }

        // split existing tensor product creating extra tensor on minimum or maximum side of current dimension
        // returns true if a an extra tensor product was inserted
        bool new_side(TensorProduct<T>&     new_tensor,             // new tensor product that started all this
                      TensorIdx             exist_tensor_idx,       // index in tensor_prods of existing tensor
                      int                   cur_dim,                // current dimension to intersect
                      KnotIdx               knot_idx,               // knot index in current dim of split point
                      int                   split_side,             // whether min (-1) or max (1) or both sides (2) of new tensor split the existing tensor
                      bool&                 knots_match,            // (output) interection resulted in a tensor whose knot mins, max match new tensor's
                      bool                  debug = false)          // print debug output
        {
            // convert global knot_idx to local_knot_idx in exist_tensor
            KnotIdx local_knot_idx = global2local_knot_idx(knot_idx, exist_tensor_idx, cur_dim);

//             if (debug)
//                 fmt::print(stderr, "new_side(): cur_dim {} knot_idx {} local_knot_idx {}\n", cur_dim, knot_idx, local_knot_idx);

            TensorProduct<T>& exist_tensor  = tensor_prods[exist_tensor_idx];

            // intialize a new side_tensor for the minimum or maximum side of the existing tensor
            TensorProduct<T> side_tensor(dom_dim_);
            side_tensor.knot_mins   = exist_tensor.knot_mins;
            side_tensor.knot_maxs   = exist_tensor.knot_maxs;
            if (split_side == -1 || split_side == 2)
            {
                side_tensor.knot_mins[cur_dim]  = knot_idx;
                exist_tensor.knot_maxs[cur_dim] = knot_idx;
            }
            else
            {
                side_tensor.knot_maxs[cur_dim]  = knot_idx;
                exist_tensor.knot_mins[cur_dim] = knot_idx;
            }
            side_tensor.level           = exist_tensor.level;
            TensorIdx side_tensor_idx   = tensor_prods.size();                  // index of new tensor to be added

//             if (debug)
//                 fmt::print("new_side(): 0: checking subset side_tensor mins [{}] side_tensor maxs [{}] new_tensor mins [{}] new_tensor maxs [{}]\n",
//                         fmt::join(side_tensor.knot_mins, ","), fmt::join(side_tensor.knot_maxs, ","), fmt::join(new_tensor.knot_mins, ","), fmt::join(new_tensor.knot_maxs, ","));

            // new side tensor will be added
            if (!subset(side_tensor.knot_mins, side_tensor.knot_maxs, new_tensor.knot_mins, new_tensor.knot_maxs))
            {
                // adjust prev and nex pointers
                adjust_prev_next(exist_tensor_idx, side_tensor, side_tensor_idx, new_tensor, split_side, cur_dim);

                //  split control points between existing and max side tensors

//                 if (debug)
//                     fprintf(stderr, "new_side(): 1: calling split_ctrl_pts exist_tensor_idx=%lu cur_dim=%d split_side=%d global knot_idx = %lu local_knot_idx=%lu\n",
//                             exist_tensor_idx, cur_dim, split_side, knot_idx, local_knot_idx);

                if (split_side == -1 || split_side == 2)
                    split_ctrl_pts(exist_tensor_idx, side_tensor, cur_dim, local_knot_idx, split_side, false, true, debug);
                else
                    split_ctrl_pts(exist_tensor_idx, side_tensor, cur_dim, local_knot_idx, split_side, false, false, debug);

                // update tensor knot indices
                tensor_knot_idxs(tensor_prods[exist_tensor_idx]);
                tensor_knot_idxs(side_tensor);

                // add the new max side tensor
                tensor_prods.push_back(side_tensor);

                // reset the reference, which could be invalid after the push_back
                TensorProduct<T>& et  = tensor_prods[exist_tensor_idx];

                // delete next and prev pointers of existing tensor that are no longer valid as a result of adding new max side
                delete_old_pointers(exist_tensor_idx);

                // check if the knot mins, maxs of the existing or added tensor match the original new tensor
                if ( (side_tensor.knot_mins == new_tensor.knot_mins && side_tensor.knot_maxs == new_tensor.knot_maxs) ||
                     (et.knot_mins == new_tensor.knot_mins && et.knot_maxs == new_tensor.knot_maxs) )
                    knots_match = true;

                return true;
            }

            // new side tensor will not be added
            else
            {
                // debug
//                 fprintf(stderr, "new_side(): 2: calling split_ctrl_pts exist_tensor_idx=%lu cur_dim=%d split_side=%d global knot_idx = %lu local_knot_idx=%lu\n",
//                         exist_tensor_idx, cur_dim, split_side, knot_idx, local_knot_idx);

                if (split_side == -1 || split_side == 2)
                    split_ctrl_pts(exist_tensor_idx, new_tensor, cur_dim, local_knot_idx, split_side, true, true, debug);
                else
                    split_ctrl_pts(exist_tensor_idx, new_tensor, cur_dim, local_knot_idx, split_side, true, false, debug);

                // update tensor knot indices
                tensor_knot_idxs(tensor_prods[exist_tensor_idx]);

                // delete next and prev pointers of existing tensor that are no longer valid as a result of adding new max side
                delete_old_pointers(exist_tensor_idx);

                return false;
            }
        }

        // adjust prev and next pointers for splitting a tensor into two
        void adjust_prev_next(
                TensorIdx           exist_tensor_idx,       // idx of existing tensor after split
                TensorProduct<T>&   side_tensor,            // new side tensor after split, not update in tensor_prods yet
                                                            // use this reference instead of tensor_prods[side_tensor_idx]
                TensorIdx           side_tensor_idx,        // idx of new side tensor after split
                TensorProduct<T>&   new_tensor,             // new tensor being added that started all this
                int                 split_side,             // whether min (-1) or max (1) or both sides (2) of new tensor split the existing tensor
                int                 cur_dim)                // current dimension to intersect
        {
            TensorProduct<T>&   exist_tensor    = tensor_prods[exist_tensor_idx];
            TensorProduct<T>*   min_tensor;
            TensorProduct<T>*   max_tensor;
            TensorIdx           min_tensor_idx;
            TensorIdx           max_tensor_idx;

            // figure which side is min and which is max
            if (split_side == 1)
            {
                min_tensor      = &side_tensor;
                max_tensor      = &exist_tensor;
                min_tensor_idx  = side_tensor_idx;
                max_tensor_idx  = exist_tensor_idx;
            }
            else
            {
                min_tensor      = &exist_tensor;
                max_tensor      = &side_tensor;
                min_tensor_idx  = exist_tensor_idx;
                max_tensor_idx  = side_tensor_idx;
            }

            // adjust next and prev pointers for min and max tensors in the current dimension
            for (int i = 0; i < min_tensor->next[cur_dim].size(); i++)
            {
                if (adjacent(*max_tensor, tensor_prods[min_tensor->next[cur_dim][i]], cur_dim))
                {
                    max_tensor->next[cur_dim].push_back(min_tensor->next[cur_dim][i]);
                    vector<TensorIdx>& prev = tensor_prods[min_tensor->next[cur_dim][i]].prev[cur_dim];
                    auto it                 = find(prev.begin(), prev.end(), min_tensor_idx);
                    assert(it != prev.end());       // sanity
                    size_t k                = distance(prev.begin(), it);
                    prev[k]                 = max_tensor_idx;
                }
            }
            for (int i = 0; i < max_tensor->prev[cur_dim].size(); i++)
            {
                if (adjacent(*min_tensor, tensor_prods[max_tensor->prev[cur_dim][i]], cur_dim))
                {
                    min_tensor->prev[cur_dim].push_back(max_tensor->prev[cur_dim][i]);
                    vector<TensorIdx>& next = tensor_prods[max_tensor->prev[cur_dim][i]].next[cur_dim];
                    auto it                 = find(next.begin(), next.end(), max_tensor_idx);
                    assert(it != next.end());       // sanity
                    size_t k                = distance(next.begin(), it);
                    next[k]                 = min_tensor_idx;
                }
            }

            // connect next and prev pointers of existing and new side tensors only if
            // the new tensor will not completely separate the two
            if (!occluded(new_tensor, *min_tensor, cur_dim))
            {
                min_tensor->next[cur_dim].push_back(max_tensor_idx);
                max_tensor->prev[cur_dim].push_back(min_tensor_idx);
            }

            // adjust next and prev pointers for min and max tensors in other dimensions
            for (int j = 0; j < dom_dim_; j++)
            {
                if (j == cur_dim)
                    continue;

                // next pointer
                for (int i = 0; i < exist_tensor.next[j].size(); i++)
                {
                    // add new next pointers
                    if (adjacent(side_tensor, tensor_prods[exist_tensor.next[j][i]], j))
                    {
                        side_tensor.next[j].push_back(exist_tensor.next[j][i]);
                        tensor_prods[exist_tensor.next[j][i]].prev[j].push_back(side_tensor_idx);
                    }

                }

                // prev pointer
                for (int i = 0; i < exist_tensor.prev[j].size(); i++)
                {
                    // add new prev pointers
                    if (adjacent(side_tensor, tensor_prods[exist_tensor.prev[j][i]], j))
                    {
                        side_tensor.prev[j].push_back(exist_tensor.prev[j][i]);
                        tensor_prods[exist_tensor.prev[j][i]].next[j].push_back(side_tensor_idx);
                    }

                }
            }
        }

        // convert global knot_idx to local_knot_idx in existing_tensor
        KnotIdx global2local_knot_idx(KnotIdx   knot_idx,
                                      TensorIdx existing_tensor_idx,
                                      int       cur_dim)
        {
            KnotIdx local_knot_idx  = 0;
            int     cur_level       = tensor_prods[existing_tensor_idx].level;
            KnotIdx min_idx         = tensor_prods[existing_tensor_idx].knot_mins[cur_dim];
            KnotIdx max_idx         = tensor_prods[existing_tensor_idx].knot_maxs[cur_dim];

            if (knot_idx < min_idx || knot_idx > max_idx)
            {
                fprintf(stderr, "Error: in global2local_knot_idx, knot_idx is not within min, max knot_idx of existing tensor\n");
                abort();
            }

            for (auto i = min_idx; i < knot_idx; i++)
                if (all_knot_levels[cur_dim][i] <= cur_level)
                    local_knot_idx++;

            return local_knot_idx;
        }

        // split control points between existing and new side tensors
        void split_ctrl_pts(TensorIdx            existing_tensor_idx,    // index in tensor_prods of existing tensor
                            TensorProduct<T>&    new_side_tensor,        // new side tensor
                            int                  cur_dim,                // current dimension to intersect
                            KnotIdx              split_knot_idx,         // local (not global!) knot index in current dim of split point in existing tensor
                            int                  split_side,             // whether min (-1) or max (1) or both sides (2) of new tensor split the existing tensor
                            bool                 skip_new_side,          // don't add control points to new_side tensor, only adjust exsiting tensor control points
                            bool                 max_side,               // new side is in the max. direction (false = min. direction)
                            bool                 debug = false)          // print debug info
        {
            TensorProduct<T>& existing_tensor = tensor_prods[existing_tensor_idx];

            // index of min and max in new side or existing tensor (depending on max_side true/false) control points in current dim
            // allowed to be negative in order for the logic below to partition correctly (long long instead of size_t)
            long long min_ctrl_idx; // index of min (max_side = true: in new side; max_side = false: in existing tensor) control points in current dim
            long long max_ctrl_idx; // index of max (max_side = true: in existing tensor; max_side = false: in new side) control points in current dim

            // convert split_knot_idx to ctrl_pt_idx
            min_ctrl_idx = split_knot_idx;
            max_ctrl_idx = split_knot_idx;

            // TODO: unclear why the following is needed, but things break if we don't do this
            if (p_(cur_dim) % 2 == 0)                           // even degree
                max_ctrl_idx--;

            // if tensor starts at global minimum, skip the first (p + 1) / 2 knots
            if ((max_side  && existing_tensor.knot_mins[cur_dim] == 0) ||
                (!max_side && new_side_tensor.knot_mins[cur_dim] == 0))
            {
                min_ctrl_idx -= (p_(cur_dim) + 1) / 2;
                max_ctrl_idx -= (p_(cur_dim) + 1) / 2;
            }

//             if (debug)
//                 fmt::print(stderr, "split_ctrl_pts(): existing tensor idx {} cur_dim {} existing tensor nctrl pts({}): {} min_ctrl_idx {}\n",
//                         existing_tensor_idx, cur_dim, cur_dim, existing_tensor.nctrl_pts(cur_dim), min_ctrl_idx);

            // if max_ctrl_idx is past last existing control point, then split is too close to global edge and must be clamped to last control point
            // NB there is no equivalent for !max_side because new_side_tensor does not have any numbers of control points assigned yet
            if (max_side && max_ctrl_idx >= existing_tensor.nctrl_pts[cur_dim])
                max_ctrl_idx = existing_tensor.nctrl_pts[cur_dim] - 1;

//             if (debug)
//             {
//                 fmt::print(stderr, "split_ctrl_pts(): splitting ctrl points in dim {} max_side {} split_knot_idx {} max_ctrl_idx {} min_ctrl_idx {}\n",
//                         cur_dim, max_side, split_knot_idx, max_ctrl_idx, min_ctrl_idx);
//                 fmt::print(stderr, "split_ctrl_pts(): old existing tensor tot_nctrl_pts {} nctrl_pts[{}] {}\n",
//                         existing_tensor.ctrl_pts.rows(), cur_dim, existing_tensor.nctrl_pts[cur_dim]);
//             }

            // allocate new control point matrix for existing tensor
            size_t tot_nctrl_pts = 1;
            VectorXi new_exist_nctrl_pts(dom_dim_);
            for (auto i = 0; i < dom_dim_; i++)
            {
                if (i != cur_dim)
                    new_exist_nctrl_pts(i) = existing_tensor.nctrl_pts(i);
                else
                {
                    if (max_side)
                        new_exist_nctrl_pts(i) = max_ctrl_idx + 1;
                    else
                        new_exist_nctrl_pts(i) = existing_tensor.nctrl_pts(i) - min_ctrl_idx;
                }
                tot_nctrl_pts *= new_exist_nctrl_pts(i);

//                 if (debug)
//                     fmt::print(stderr, "split_ctrl_pts(): i {} tot_nctrl_pts {} new_exist_ctrl_pts[{}] {}\n",
//                             i, tot_nctrl_pts, i, new_exist_nctrl_pts(i));
            }
            MatrixX<T> new_exist_ctrl_pts(tot_nctrl_pts, max_dim_ - min_dim_ + 1);
            VectorX<T> new_exist_weights(tot_nctrl_pts);

            // allocate new control point matrix for new side tensor
            if (!skip_new_side)
            {
                tot_nctrl_pts = 1;
                for (auto i = 0; i < dom_dim_; i++)
                {
                    if (i != cur_dim)
                        new_side_tensor.nctrl_pts(i) = existing_tensor.nctrl_pts(i);
                    else
                    {
                        if (max_side)
                            new_side_tensor.nctrl_pts(i) = existing_tensor.nctrl_pts(i) - min_ctrl_idx;
                        else
                            new_side_tensor.nctrl_pts(i) = max_ctrl_idx + 1;
                    }
                    tot_nctrl_pts *= new_side_tensor.nctrl_pts(i);
                }
                new_side_tensor.ctrl_pts.resize(tot_nctrl_pts, max_dim_ - min_dim_ + 1);
                new_side_tensor.weights.resize(tot_nctrl_pts);
            }

            // split the control points
            VolIterator vol_iter(existing_tensor.nctrl_pts);            // for iterating in a flat loop over n dimensions
            size_t      cur_exist_idx    = 0;                           // current index into new_exist_ctrl_pts and weights
            size_t      cur_new_side_idx = 0;                           // current index into new_side_tensor.ctrl_pts and weights
            while (!vol_iter.done())
            {
                // control point goes either to existing or new side tensor depending on index in current dimension

                // control point goes to existing tensor
                if ((max_side && vol_iter.idx_dim(cur_dim) <= max_ctrl_idx) || (!max_side && vol_iter.idx_dim(cur_dim) >= min_ctrl_idx))
                {
                    // debug
//                     fprintf(stderr, "moving to new_exist_ctrl_pts[%lu]\n", cur_exist_idx);

                    new_exist_ctrl_pts.row(cur_exist_idx) = existing_tensor.ctrl_pts.row(vol_iter.cur_iter());
                    new_exist_weights(cur_exist_idx) = existing_tensor.weights(vol_iter.cur_iter());

                    // when degree is odd, set the ctrl point to nan, real value is in the new side tensor
                    // TODO: check for being outside the new tensor bounds in the other dims
                    if (split_side != 2 && p_(cur_dim) % 2 &&
                            ((max_side && vol_iter.idx_dim(cur_dim) == max_ctrl_idx) ||
                             (!max_side && vol_iter.idx_dim(cur_dim) == min_ctrl_idx)))
                        new_exist_weights(cur_exist_idx, 0) = MFA_NAW;

                    cur_exist_idx++;
                }

                // control point goes to new side tensor
                if ((max_side && vol_iter.idx_dim(cur_dim) >= min_ctrl_idx) || (!max_side && vol_iter.idx_dim(cur_dim) <= max_ctrl_idx))
                {
                    if (!skip_new_side)
                    {
                        // debug
//                         fprintf(stderr, "moving to new_side_tensor.ctrl_pts[%lu]\n", cur_new_side_idx);

                        new_side_tensor.ctrl_pts.row(cur_new_side_idx) = existing_tensor.ctrl_pts.row(vol_iter.cur_iter());
                        new_side_tensor.weights(cur_new_side_idx) = existing_tensor.weights(vol_iter.cur_iter());

                        // when degree is odd and new tensor is inside the existing tensor,
                        // set the ctrl point to nan, real value is in the added tensor
                        // TODO: check for being outside the new tensor bounds in the other dims
                        if (split_side == 2 && p_(cur_dim) % 2 &&
                                ((max_side && vol_iter.idx_dim(cur_dim) == min_ctrl_idx) ||
                                 (!max_side && vol_iter.idx_dim(cur_dim) == max_ctrl_idx)))
                            new_side_tensor.weights(cur_new_side_idx, 0) = MFA_NAW;
                    }

                    cur_new_side_idx++;
                }

                vol_iter.incr_iter();                                   // must increment volume iterator at the bottom of the loop
            }

            // copy new_exist_ctrl_pts and weights to existing_tensor.ctrl_pts and weights, resizes automatically
            existing_tensor.ctrl_pts    = new_exist_ctrl_pts;
            existing_tensor.weights     = new_exist_weights;
            existing_tensor.nctrl_pts   = new_exist_nctrl_pts;

            // debug
//             fprintf(stderr, "new existing tensor tot_nctrl_pts=%lu = [%d %d]\n", existing_tensor.ctrl_pts.rows(), existing_tensor.nctrl_pts[0], existing_tensor.nctrl_pts[1]);
//             if (!skip_new_side)
//                 fprintf(stderr, "max side tensor tot_nctrl_pts=%lu = [%d %d]\n\n", new_side_tensor.ctrl_pts.rows(), new_side_tensor.nctrl_pts[0], new_side_tensor.nctrl_pts[1]);
        }

        // delete pointer from prev and/or next vectors of a tensor
        void delete_pointer(TensorIdx t_idx,                            // existing tensor
                            TensorIdx p_idx)                            // pointer to be deleted from prev and next
        {
            for (auto i = 0; i < dom_dim_; i++)
            {
                // delete from prev
                // does not check for duplicates, only deletes first instance found
                auto& prev = tensor_prods[t_idx].prev[i];
                for (auto j = 0; j < prev.size(); j++)
                {
                    if (prev[j] == p_idx)
                    {
                        prev[j] = prev.back();
                        prev.resize(prev.size() - 1);
                        break;
                    }
                }

                // delete from next
                // does not check for duplicates, only deletes first instance found
                auto& next = tensor_prods[t_idx].next[i];
                for (auto j = 0; j < next.size(); j++)
                {
                    if (next[j] == p_idx)
                    {
                        next[j] = next.back();
                        next.resize(next.size() - 1);
                        break;
                    }
                }
            }
        }

        // change pointer from prev and/or next vectors of a tensor
        void change_pointer(TensorIdx t_idx,                            // existing tensor
                            TensorIdx old_idx,                          // pointer to be changed from prev and next of existing tensor
                            TensorIdx new_idx)                          // new value of pointer
        {
            for (auto i = 0; i < dom_dim_; i++)
            {
                // check prev
                auto& prev = tensor_prods[t_idx].prev[i];
                for (auto j = 0; j < prev.size(); j++)
                {
                    if (prev[j] == old_idx)
                        prev[j] = new_idx;
                }

                // check next
                auto& next = tensor_prods[t_idx].next[i];
                for (auto j = 0; j < next.size(); j++)
                {
                    if (next[j] == old_idx)
                        next[j] = new_idx;
                }
            }
        }

        // delete pointers that are no longer valid as a result of adding a new max side tensor
        void delete_old_pointers(TensorIdx existing_tensor_idx)         // index in tensor_prods of existing tensor
        {
            TensorProduct<T>& existing_tensor  = tensor_prods[existing_tensor_idx];

            for (int j = 0; j < dom_dim_; j++)
            {
                // next pointer
                size_t valid_size = existing_tensor.next[j].size();     // size excluding invalid entries at back
                for (int i = 0; i < valid_size; i++)
                {
                    if (!adjacent(existing_tensor, tensor_prods[existing_tensor.next[j][i]], j))
                    {
                        // debug
//                         fprintf(stderr, "next tensor %lu is no longer adjacent to existing_tensor %lu\n",
//                                 existing_tensor.next[j][i], existing_tensor_idx);

                        // remove the prev pointer of the next tensor
                        vector<TensorIdx>& prev = tensor_prods[existing_tensor.next[j][i]].prev[j];
                        auto it                 = find(prev.begin(), prev.end(), existing_tensor_idx);
                        if (it != prev.end())                       // it's possible the pointer was removed earlier and won't be found
                        {
                            size_t k                = distance(prev.begin(), it);
                            prev[k] = prev.back();
                            prev.resize(prev.size() - 1);
                        }

                        // remove the next pointer of the existing tensor
                        existing_tensor.next[j][i] = existing_tensor.next[j][valid_size - 1];
                        valid_size--;
                        i--;                                        // keep loop counter the same for next iteration
                    }
                }
                existing_tensor.next[j].resize(valid_size);         // drop the invalid entries at back

                // prev pointer
                valid_size = existing_tensor.prev[j].size();        // size excluding invalid entries at back
                for (int i = 0; i < valid_size; i++)
                {
                    if (!adjacent(existing_tensor, tensor_prods[existing_tensor.prev[j][i]], j))
                    {
                        // debug
//                         fprintf(stderr, "prev tensor %lu is no longer adjacent to existing_tensor %lu\n",
//                                 existing_tensor.prev[j][i], existing_tensor_idx);

                        // remove the next pointer of the prev tensor
                        vector<TensorIdx>& next = tensor_prods[existing_tensor.prev[j][i]].next[j];
                        auto it                 = find(next.begin(), next.end(), existing_tensor_idx);
                        if (it != next.end())                       // it's possible the pointer was removed earlier and won't be found
                        {
                            size_t k                = distance(next.begin(), it);
                            next[k] = next.back();
                            next.resize(next.size() - 1);
                        }

                        // remove the prev pointer of the existing tensor
                        existing_tensor.prev[j][i] = existing_tensor.prev[j][valid_size - 1];
                        valid_size--;
                        i--;                                        // keep loop counter the same for next iteration
                    }
                }
                existing_tensor.prev[j].resize(valid_size);         // drop the invalid entries at back
            }
        }

        // check if new_tensor is adjacent to existing_tensor in current dimension
        // returns -1: existing_tensor is adjacent on min side of new_tensor in current dim.
        //          0: not adjacent
        //          1: existing_tensor is adjacent on max side of new_tensor in current dim.
        int adjacent(TensorProduct<T>&      new_tensor,       // new tensor product to be added
                     TensorProduct<T>&      existing_tensor,  // existing tensor product
                     int                    cur_dim)          // current dimension
        {
            int retval = 0;

            // check if adjacency exists in current dim
            if (new_tensor.knot_mins[cur_dim] == existing_tensor.knot_maxs[cur_dim])
                retval = -1;
            else if (new_tensor.knot_maxs[cur_dim] == existing_tensor.knot_mins[cur_dim])
                retval = 1;
            else
            {
                // debug
//                 fprintf(stderr, "adj 1: cur_dim=%d retval=0 new_tensor=[%lu %lu : %lu %lu] existing_tensor=[%lu %lu : %lu %lu]\n",
//                         cur_dim, new_tensor.knot_mins[0], new_tensor.knot_mins[1], new_tensor.knot_maxs[0], new_tensor.knot_maxs[1],
//                         existing_tensor.knot_mins[0], existing_tensor.knot_mins[1], existing_tensor.knot_maxs[0], existing_tensor.knot_maxs[1]);

                return 0;
            }

            // confirm that intersection in at least one other dimension exists
            for (int j = 0; j < dom_dim_; j++)
            {
                if (j == cur_dim)
                    continue;

                // the area touching is zero in some dimension
                // two cases are checked because role of new and existing tensor can be interchanged for adjacency; both need to fail to be nonadjacent
                if ( (new_tensor.knot_mins[j]      < existing_tensor.knot_mins[j] || new_tensor.knot_mins[j]      >= existing_tensor.knot_maxs[j]) &&
                     (existing_tensor.knot_mins[j] < new_tensor.knot_mins[j]      || existing_tensor.knot_mins[j] >= new_tensor.knot_maxs[j]))
                {

                    // debug
//                     fprintf(stderr, "adj 2: cur_dim=%d retval=0 new_tensor=[%lu %lu : %lu %lu] existing_tensor=[%lu %lu : %lu %lu]\n",
//                             cur_dim, new_tensor.knot_mins[0], new_tensor.knot_mins[1], new_tensor.knot_maxs[0], new_tensor.knot_maxs[1],
//                             existing_tensor.knot_mins[0], existing_tensor.knot_mins[1], existing_tensor.knot_maxs[0], existing_tensor.knot_maxs[1]);

                    return 0;
                }
            }

            // debug
//             fprintf(stderr, "adj: cur_dim=%d retval=%d new_tensor=[%lu %lu : %lu %lu] existing_tensor=[%lu %lu : %lu %lu]\n",
//                     cur_dim, retval, new_tensor.knot_mins[0], new_tensor.knot_mins[1], new_tensor.knot_maxs[0], new_tensor.knot_maxs[1],
//                     existing_tensor.knot_mins[0], existing_tensor.knot_mins[1], existing_tensor.knot_maxs[0], existing_tensor.knot_maxs[1]);

            return retval;
        }

        // check if new_tensor completely occludes any neighbor of existing_tensor
        // ie, returns true if the face they share is the full size of existing_tensor
        // assumes they share a face in the cur_dim (does not check)
        int occluded(TensorProduct<T>&      new_tensor,       // new tensor product to be added
                     TensorProduct<T>&      existing_tensor,  // existing tensor product
                     int                    cur_dim)          // current dimension
        {
            // confirm that new_tensor is larger than existing_tensor in every dimension except cur_dim
            for (int j = 0; j < dom_dim_; j++)
            {
                if (j == cur_dim)
                    continue;

                if(new_tensor.knot_mins[j] > existing_tensor.knot_mins[j] ||
                   new_tensor.knot_maxs[j] < existing_tensor.knot_maxs[j])
                    return false;
            }

            // debug
//             fprintf(stderr, "cur_dim=%d return=true new_tensor=[%lu %lu : %lu %lu] existing_tensor=[%lu %lu : %lu %lu]\n",
//                     cur_dim, new_tensor.knot_mins[0], new_tensor.knot_mins[1], new_tensor.knot_maxs[0], new_tensor.knot_maxs[1],
//                     existing_tensor.knot_mins[0], existing_tensor.kot_mins[1], existing_tensor.knot_maxs[0], existing_tensor.knot_maxs[1]);

            return true;
        }

        // checks if a_mins, maxs are a subset of b_mins, maxs
        // identical bounds counts as a subset (does not need to be proper subset)
        bool subset(const vector<KnotIdx>& a_mins,
                    const vector<KnotIdx>& a_maxs,
                    const vector<KnotIdx>& b_mins,
                    const vector<KnotIdx>& b_maxs) const
        {
            // check that sizes are identical
            size_t a_size = a_mins.size();
            if (a_size != a_maxs.size() || a_size != b_mins.size() || a_size != b_maxs.size())
            {
                fprintf(stderr, "Error, size mismatch in subset()\n");
                abort();
            }

            // check subset condition
            for (auto i = 0; i < a_size; i++)
                if (a_mins[i] < b_mins[i] || a_maxs[i] > b_maxs[i])
                        return false;

            // debug
//             fprintf(stderr, "[%lu %lu : %lu %lu] is a subset of [%lu %lu : %lu %lu]\n",
//                     a_mins[0], a_mins[1], a_maxs[0], a_maxs[1], b_mins[0], b_mins[1], b_maxs[0], b_maxs[1]);

            return true;
        }

        // forms union of mins and maxs of a and b and stores result in res
        void merge(const vector<KnotIdx>& a_mins,
                   const vector<KnotIdx>& a_maxs,
                   const vector<KnotIdx>& b_mins,
                   const vector<KnotIdx>& b_maxs,
                   vector<KnotIdx>&       res_mins,
                   vector<KnotIdx>&       res_maxs)
        {
            // check that sizes are identical
            size_t a_size = a_mins.size();
            if (a_size != a_maxs.size() || a_size != b_mins.size() || a_size != b_maxs.size())
            {
                fprintf(stderr, "Error, size mismatch in subset()\n");
                abort();
            }

            res_mins.resize(a_size);
            res_maxs.resize(a_size);

            // form union
            for (auto i = 0; i < a_size; i++)
            {
                res_mins[i] = a_mins[i] < b_mins[i] ? a_mins[i] : b_mins[i];
                res_maxs[i] = a_maxs[i] > b_maxs[i] ? a_maxs[i] : b_maxs[i];
            }
        }

        // constrain tensor knot_mins, knot_maxs to parent of tensor, if closer to parent than pad
        void constrain_to_parent(
                TensorProduct<T>&   t,          // tensor to constrain
                int                 pad)        // constrain to parent if tensor is within pad of parent or greater
        {
            auto& parent = tensor_prods[t.parent];
            for (auto j = 0; j < dom_dim_; j++)
            {
                int edge_pad = (p_(j) / 2) + 1;                                 // extra padding for tensor at the global edge
                int ofst = (t.knot_mins[j] == 0) ? pad + edge_pad : pad;
                if (t.knot_mins[j] < parent.knot_mins[j] ||
                        knot_idx_dist(parent, parent.knot_mins[j], t.knot_mins[j], j, false) < ofst)
                    t.knot_mins[j] = parent.knot_mins[j];
                ofst = (t.knot_maxs[j] == all_knots[j].size() - 1) ? pad + edge_pad : pad;
                if (t.knot_maxs[j] > parent.knot_maxs[j] ||
                        knot_idx_dist(parent, t.knot_maxs[j], parent.knot_maxs[j], j, false) < ofst)
                    t.knot_maxs[j] = parent.knot_maxs[j];
            }
        }

        // merges two tensor product knot mins, maxs, optionally constrained by parent of resulting tensor
        void merge_tensors(
                TensorProduct<T>&   inout,      // one of the input tensors and the output of the merge
                TensorProduct<T>&   in,         // other input tensor
                int                 pad)        // constrain merge to parent of inout if merge is within pad of parent; -1: don't constrain to parent
        {
            vector<KnotIdx> merge_mins(dom_dim_);
            vector<KnotIdx> merge_maxs(dom_dim_);
            merge(inout.knot_mins, inout.knot_maxs, in.knot_mins, in.knot_maxs, merge_mins, merge_maxs);
            inout.knot_mins = merge_mins;   // adjust t to the merged extents
            inout.knot_maxs = merge_maxs;

            // don't overshoot the parent or leave it with a small remainder
            if (pad >= 0)
                constrain_to_parent(inout, pad);
        }

        // checks if a point in index space is in a tensor product
        // in all dimensions except skip_dim (-1 = default, don't skip any dimensions)
        // if ctrl_pt_anchor == true, for dimension i, if degree[i] is even, pt[i] + 0.5 is checked because pt coords are truncated to integers
        bool in(const vector<KnotIdx>&  pt,
                const TensorProduct<T>& tensor,
                bool                    ctrl_pt_anchor,         // whether pt refers to control point anchor (shifted 1/2 space for even degree)
                int                     skip_dim = -1) const
        {
            // debug
//             bool debug;
//             if (pt[0] == 6 and pt[1] == 2)
//                 debug = true;

            for (auto i = 0; i < pt.size(); i++)
            {
                if (i == skip_dim)
                    continue;

                // move pt[i] to center of t-mesh cell if degree is even and pt refers to control point anchor
                float fp;               // floating point version of pt[i]
                if (ctrl_pt_anchor && p_(i) % 2 == 0)
                    fp = pt[i] + 0.5;
                else
                    fp = pt[i];

                if (fp < float(tensor.knot_mins[i]) || fp > float(tensor.knot_maxs[i]))
                        return false;
            }

            return true;
        }

        // checks if a point in index space is in a tensor product in one dimension
        // for dimension i, if degree[i] is even, pt[i] + 0.5 is checked because pt coords are truncated to integers
        // if ctrl_pt_anchor == true, if degree[cur_dim] is even, pt + 0.5 is checked because pt coords are truncated to integers
        bool in_dim(KnotIdx                 pt,
                    const TensorProduct<T>& tensor,
                    bool                    ctrl_pt_anchor,         // whether pt refers to control point anchor (shifted 1/2 space for even degree)
                    int                     cur_dim) const
        {
            // move pt to center of t-mesh cell if degree is even and pt refers to control point anchor
            float fp;               // floating point version of pt
            if (ctrl_pt_anchor && p_(cur_dim) % 2 == 0)
                fp = pt + 0.5;
            else
                fp = pt;
            return(fp >= float(tensor.knot_mins[cur_dim]) && fp <= float(tensor.knot_maxs[cur_dim]));
        }

        // checks if a point in index space is in the neighbors of a tensor product, for one dimension
        void in_neighbors(const vector<KnotIdx>&    pt,                     // anchor point in index space
                          int                       cur_dim,                // current dimension
                          int                       tensor_idx,             // index of current tensor
                          vector<int>&              neighbor_idxs) const    // (output) neighbor tensor idxs containing the point in cur_dim
        {
            const TensorProduct<T>& t = tensor_prods[tensor_idx];

            for (auto i = 0; i < t.prev[cur_dim].size(); i++)
            {
                const TensorProduct<T>&   tp = tensor_prods[t.prev[cur_dim][i]];
                if (pt[cur_dim] >= tp.knot_mins[cur_dim] && pt[cur_dim] <= tp.knot_maxs[cur_dim])
                    neighbor_idxs.push_back(t.prev[cur_dim][i]);
            }

            for (auto i = 0; i < t.next[cur_dim].size(); i++)
            {
                const TensorProduct<T>&   tn = tensor_prods[t.next[cur_dim][i]];
                if (pt[cur_dim] >= tn.knot_mins[cur_dim] && pt[cur_dim] <= tn.knot_maxs[cur_dim])
                    neighbor_idxs.push_back(t.next[cur_dim][i]);
            }
        }

        // given an anchor in index space, find intersecting knot lines in index space
        // in -/+ directions in all dimensions
        void knot_intersections(const vector<KnotIdx>&      anchor,                 // knot indices of anchor for odd degree or
                                                                                    // knot indices of start of rectangle containing anchor for even degree
                                TensorIdx                   t_idx,                  // index of tensor product containing anchor
                                bool                        ctrl_pt_anchor,         // whether pt refers to control point anchor (shifted 1/2 space for even degree)
                                vector<vector<KnotIdx>>&    loc_knots) const        // (output) local knot vector in index space
        {
            // debug
            bool debug = false;
//             if (anchor[0] == 78 && anchor[1] == 41 && t_idx == 5)
//                 debug = true;

            // sanity check that anchor is in the current tensor
            const TensorProduct<T>& t = tensor_prods[t_idx];
            for (auto j = 0; j < dom_dim_; j++)
            {
                if (anchor[j] < t.knot_mins[j] || anchor[j] > t.knot_maxs[j])
                {
                    fmt::print(stderr, "Error: knot_intersections(): anchor [{}] is outside of tensor {} knot mins [{}] maxs [{}]. This should not happen.\n",
                            fmt::join(anchor, ","), t_idx, fmt::join(t.knot_mins, ","), fmt::join(t.knot_maxs, ","));
                    abort();
                }
            }

            loc_knots.resize(dom_dim_);
            assert(anchor.size() == dom_dim_);

            int max_level = tensor_prods[t_idx].level;                      // level of tensor product

            vector<NeighborTensor> unused;

            // walk the t-mesh in all dimensions, min. and max. directions outward from the anchor
            // looking for interecting knot lines

            for (auto i = 0; i < dom_dim_; i++)                             // for all dims
            {
                loc_knots[i].resize(p_(i) + 2);                             // support of basis func. is p+2 knots (p+1 spans) by definition

                KnotIdx start, min, max;
                if (p_(i) % 2)                                              // odd degree
                    start = min = max = (p_(i) + 1) / 2;
                else                                                        // even degree
                {
                    start = min = p_(i) / 2;
                    max         = p_(i) / 2 + 1;
                }
                loc_knots[i][start]             = anchor[i];                // start at the anchor
                KnotIdx         cur_knot_idx    = loc_knots[i][start];
                TensorIdx       cur_tensor      = t_idx;
                int             cur_level       = max_level;
                vector<KnotIdx> cur             = anchor;                   // current knot location in the tmesh (index space)

                // from the anchor in the min. direction
                for (int j = 0; j < min; j++)                               // add 'min' more knots in minimum direction from the anchor
                {
                    // debug
//                     if (anchor[0] == 6 && anchor[1] == 9 && i == 1 && t_idx == 5)
//                         debug = true;

//                     if (debug)
//                         fmt::print(stderr, "knot_intersections() min. dir. dim {} j {} min {} before neighbor_tensor_ofst: cur [{}] cur_tensor {} cur_level {}\n",
//                                 i, j, min, fmt::join(cur, ","), cur_tensor, cur_level);

                    // find the next knot and the tensor containing it
                    int count       = 0;
                    int max_count   = 10;
                    while (cur[i] > 0 && !next_inter(tensor_prods[cur_tensor].prev[i], i, -1, cur, cur_tensor, cur_level) && count < max_count)
                        count++;
                    if (count >= max_count)
                    {
                        fmt::print(stderr, "Error: knot_intersections(): max attempts at constructing local knot vector in min. direction exceeded\n");
                        fmt::print(stderr, "dim {} anchor {} t_idx {}\n", i, fmt::join(anchor, ","), t_idx);
                        abort();
                    }

//                     if (debug)
//                         fmt::print(stderr, "knot_intersections() min. dir. dim {} j {} min {} after neighbor_tensor_ofst: cur [{}] cur_tensor {} cur_level {}\n",
//                                 i, j, min, fmt::join(cur, ","), cur_tensor, cur_level);

                    if (cur[i] > 0)                                         // more knots in the tmesh
                    {
                        // check if next knot borders a higher level; if so, switch to higher level tensor
                        border_higher_level(cur, ctrl_pt_anchor, cur_tensor, cur_level);

                        // record the knot
                        loc_knots[i][start - j - 1] = cur[i];
                    }
                    else                                                    // no more knots in the tmesh
                        loc_knots[i][start - j - 1] = 0;                    // repeat first index as many times as needed

                }       // for j knots

                // reset back to anchor
                cur_knot_idx    = loc_knots[i][start];
                cur_tensor      = t_idx;
                cur_level       = max_level;
                cur             = anchor;

                // from the anchor in the max. direction
                for (int j = 0; j < max; j++)                               // add 'max' more knots in maximum direction from the anchor
                {
//                     if (anchor[0] == 78 && anchor[1] == 41 && i == 1 && t_idx == 5)
//                         debug = true;

//                     if (debug)
//                         fmt::print(stderr, "knot_intersections() dim {} max. dir. before neighbor_tensor_ofst: cur [{}] cur_tensor {} cur_level {}\n",
//                                 i, fmt::join(cur, ","), cur_tensor, cur_level);

                    // find the next knot and the tensor containing it
                    int count       = 0;
                    int max_count   = 10;
                    while (cur[i] < all_knots[i].size() - 1 &&
                            !next_inter(tensor_prods[cur_tensor].next[i], i, 1, cur, cur_tensor, cur_level) && count < max_count)
                        count++;
                    if (count >= max_count)
                    {
                        fmt::print(stderr, "Error: knot_intersections(): max attempts at constructing local knot vector in max. direction exceeded\n");
                        fmt::print(stderr, "dim {} anchor {} t_idx {}\n", i, fmt::join(anchor, ","), t_idx);
                        abort();
                    }

//                     if (debug)
//                         fmt::print(stderr, "knot_intersections() dim {} max. dir. after neighbor_tensor_ofst: cur [{}] cur_tensor {} cur_level {}\n",
//                                 i, fmt::join(cur, ","), cur_tensor, cur_level);

                    if (cur[i] < all_knots[i].size() - 1)
                    {
                        // if next knot borders a higher level; switch to higher level tensor
                        border_higher_level(cur, ctrl_pt_anchor, cur_tensor, cur_level);

                        // record the knot
                        loc_knots[i][start + j + 1] = cur[i];
                    }
                    else                                                        // no more knots in the tmesh
                        loc_knots[i][start + j + 1] = all_knots[i].size() - 1;  // repeat last index as many times as needed
                }       // for j knots
            }       // for all dims
        }

        // iterates to the next intersection of knot index
        // first checks current tensor before checking its neighbors
        // if more than one tensor sharing the target, pick highest level
        // updates target, cur_tensor, and cur_level
        // returns whether a tensor was found containing the offset target
        bool next_inter(const vector<TensorIdx>& prev_next,          // previous or next neighbor tensors
                        int                      cur_dim,            // current dimension
                        int                      dir,                // direction iterate +/-1
                        vector<KnotIdx>&         target,             // (input / output) target knot indices, offset by this function
                        TensorIdx&               cur_tensor,         // (input / output) highest level neighbor tensor containing the target
                        int&                     cur_level) const    // (input / output) level of current tensor
        {
            int             new_level;
            TensorIdx       new_k;
            KnotIdx         new_target, temp_target;
            vector<KnotIdx> ofst_target = target;
            bool            success     = false;
            bool            first_time  = true;
            int             ofst;

//             bool debug = false;
//             if (target[0] == 78 && target[1] == 43 && dir == 1)
//                 debug = true;

            if (dir == 1 || dir == -1)
                ofst = dir;
            else
            {
                fmt::print(stderr, "Error: next_inter(): dir must be +/- 1\n");
                abort();
            }

            // check if the target plus offset is in the current tensor
            if (knot_idx_ofst(tensor_prods[cur_tensor], target[cur_dim], ofst, cur_dim, false, temp_target))
            {
                target[cur_dim] = temp_target;
                return true;
            }

            // move the target to the edge of the tensor before checking neighbors
            if (ofst < 0)
                target[cur_dim] = tensor_prods[cur_tensor].knot_mins[cur_dim];
            else
                target[cur_dim] = tensor_prods[cur_tensor].knot_maxs[cur_dim];

            // check if the target plus offset is in the neighbors
            for (auto k = 0; k < prev_next.size(); k++)
            {
                knot_idx_ofst(tensor_prods[prev_next[k]], target[cur_dim], ofst, cur_dim, false, temp_target);
                ofst_target[cur_dim] = temp_target;
                if (in(ofst_target, tensor_prods[prev_next[k]], false))
                {
                    if (first_time || tensor_prods[prev_next[k]].level > new_level)
                    {
                        new_k           = k;
                        new_level       = tensor_prods[prev_next[k]].level;
                        new_target      = temp_target;
                        first_time      = false;
                    }
                    success = true;
                }
            }

            if (!success)
                return false;

            // adjust the target and tensor
            target[cur_dim] = new_target;
            cur_tensor      = prev_next[new_k];
            cur_level       = tensor_prods[cur_tensor].level;

            return true;
        }

        // check if target knot index borders a higher level; if so, switch to higher level tensor
        void border_higher_level(const vector<KnotIdx>& target,             // target knot indices
                                 bool                   ctrl_pt_anchor,     // whether pt refers to control point anchor (shifted 1/2 space for even degree)
                                 TensorIdx&             cur_tensor,         // (input / output) highest level neighbor tensor containing the target
                                 int&                   cur_level) const    // (input / output) level of current tensor
        {
            for (auto k = 0; k < tensor_prods.size(); k++)
            {
                if (in(target, tensor_prods[k], ctrl_pt_anchor, -1) && tensor_prods[k].level > cur_level)
                {
                    cur_tensor  = k;
                    cur_level   = tensor_prods[k].level;
                }
            }
        }

        // given a point in parameter space to decode, compute p + 1 anchor points in all dims in knot index space
        // anchors correspond to those basis functions that cover the decoding point
        // anchors are the centers of basis functions and locations of corresponding control points, in knot index space
        // in Bazilevs 2010, knot indices start at 1, but mine start at 0
        // returns index of tensor containing the parameters of the point to decode
        TensorIdx anchors(const VectorX<T>&          param,             // parameter value in each dim. of desired point
                          vector<vector<KnotIdx>>&   anchors) const     // (output) anchor points in index space
        {
            // debug
//             bool debug = false;
//             if (fabs(param(0) - 0.383838) < 1e-4 && fabs(param(1) - 0.0) < 1e-4)
//                 debug = true;

            anchors.resize(dom_dim_);

            // convert param to target (center of p + 1 anchors) in index space
            // the target is being computed on the global knot vector, without regarding the level of
            // refinement of the tensor where the decoded point is, and adjusted afterwards.
            vector<KnotIdx> target(dom_dim_);                       // center anchor in each dim.
            for (auto i = 0; i < dom_dim_; i++)
            {
                if (param(i) < 1.0)
                {
                    auto it     = upper_bound(all_knots[i].begin() + p_(i), all_knots[i].end() - p_(i) - 1, param(i));
                    target[i]   = it - all_knots[i].begin() - 1;
                }
                else
                    target[i] = all_knots[i].size() - (p_(i) + 2);
            }

            // debug
//             if (debug)
//                 fmt::print(stderr, "anchors(): target [{}]\n", fmt::join(target, ","));

            // find most refined tensor product containing target
            bool found = false;
            TensorIdx t_idx = 0;
            int max_level   = -1;
            for (auto j = 0; j < tensor_prods.size(); j++)
            {
                if (in(target, tensor_prods[j], true, -1))
                {
                    found = true;

                    // debug
//                     if (debug)
//                         fmt::print(stderr, "anchors(): found anchor in t_idx {}\n", t_idx);

                    if (tensor_prods[j].level > max_level)
                    {
                        t_idx       = j;
                        max_level   = tensor_prods[j].level;
                    }
                }
            }
            if (!found)
            {
                fmt::print(stderr, "Error: anchors(): target [{}] not found in any tensors\n", fmt::join(target, ","));
                abort();
            }

            // debug
//             if (debug)
//                 fmt::print(stderr, "anchors(): t_idx {} max_level {}\n", t_idx, max_level);

            // adjust target to skip over any knots at a higher refinement level than the target
            for (auto i = 0; i < dom_dim_; i++)
            {
                while (target[i] > tensor_prods[t_idx].knot_mins[i] && all_knot_levels[i][target[i]] > max_level)
                        target[i]--;
            }

            // debug
//             if (debug)
//                 fmt::print(stderr, "anchors(): adjusted target [{}]\n", fmt::join(target, ","));

            // sanity check: target should be inside tensor
            const TensorProduct<T>& t = tensor_prods[t_idx];
            for (auto i = 0; i < dom_dim_; i++)
            {
                if (target[i] < t.knot_mins[i] || target[i] > t.knot_maxs[i])
                {
                    fmt::print(stderr, "Error: anchors(): in dim {}, target [{}] is not inside tensor {} knot_mins [{}] knot_maxs [{}].\n",
                            i, fmt::join(target, ","), t_idx, t.knot_mins[i], t.knot_maxs[i]);
                    cerr << "param: " << param.transpose() << endl;
                    abort();
                }
            }

            // find local knot vector (p + 2) knot intersections
            vector<vector<KnotIdx>> loc_knots(dom_dim_);
            knot_intersections(target, t_idx, true, loc_knots);

            // debug
//             if (debug)
//             {
//                 for (auto j = 0; j < dom_dim_; j++)
//                     fmt::print(stderr, "anchors(): loc_knots[{}] = [{}]\n", j, fmt::join(loc_knots[j], ","));
//             }

            // take correct p + 1 anchors out of the p + 2 found above
            for (auto i = 0; i < dom_dim_; i++)
            {
                anchors[i].resize(p_(i) + 1);
                for (auto j = 0; j < p_(i) + 1; j++)
                {
                    if (p_(i) % 2 == 0)                             // even degree
                        anchors[i][j] = loc_knots[i][j];
                    else                                            // odd degree
                        anchors[i][j] = loc_knots[i][j + 1];
                }
            }

            // debug
//             if (debug)
//             {
//                 for (auto j = 0; j < dom_dim_; j++)
//                     fmt::print(stderr, "anchors(): anchors[{}] = [{}]\n", j, fmt::join(anchors[j], ","));
//             }

            return t_idx;
        }

        // checks if a target point in index space is in the multidim range of anchors
        bool in_anchors(const vector<KnotIdx>&          target,             // target point
                        const vector<vector<KnotIdx>>&  anchors) const
        {
            // assumes anchor indices are sorted from low to high, which should always be true
            for (auto k = 0; k < anchors.size(); k++)
            {
                if (target[k] < anchors[k].front() || target[k] > anchors[k].back())
                    return false;
            }
            return true;
        }

        // scatter global set of control points into their proper tensor products in the tmesh
        void scatter_ctrl_pts(const VectorXi&           nctrl_pts,          // number of control points in each dim.
                              const MatrixX<T>&         ctrl_pts,           // control points
                              const VectorX<T>&         weights)            // weights
        {
            // 2-step algorithm:
            // 1. Count required number of control points required to be allocated in each tensor,
            // recording destination tensor. Skip control points that are extensions of refined tensors.
            // 2. Copy the control points to the tensors using the recorded destination tensor for each control point

            vector<KnotIdx>     anchor(dom_dim_);                           // anchor in each dim. of control point in global tensor
            TensorIdx           tensor_idx = 0;                             // index of current tensor
            vector<int>         start_tensor_idx(dom_dim_, 0);              // index of starting tensor when dimension changes
            vector<size_t>      tensor_tot_nctrl_pts(tensor_prods.size());  // number of control points needed to allocate in each tensor
            vector<size_t>      tensor_cur_nctrl_pts(tensor_prods.size());  // current number of control points in each tensor so far
            vector<vector<int>> tensor_idxs(ctrl_pts.rows());               // destination tensors for each control point, empty if skipping point

            VolIterator         vol_iter(nctrl_pts);                        // iterator over control points
            VectorXi            ijk(dom_dim_);                              // multidim index of current control point

            while (!vol_iter.done())
            {
                assert(tensor_idx < tensor_prods.size());                   // sanity: anchor was found in some tensor

                vol_iter.idx_ijk(vol_iter.cur_iter(), ijk);

                // get anchor of the current control point
                for (auto j = 0; j < dom_dim_; j++)
                    anchor[j] = ijk(j) + (p_(j) + 1) / 2;                  // first control point has anchor floor((p + 1) / 2)

                // debug
//                 cerr << "ctrl_pt(" << vol_iter.cur_iter() << ") ijk [ " << ijk.transpose() << " ] anchor [ " << anchor[0] << " " << anchor[1] <<
//                     " ] value " << ctrl_pts.row(vol_iter.cur_iter()) << " found in tensor_idx " << tensor_idx << endl;

                // find minimum level of all the dimensions of the anchor of the control point
                int anchor_max_level;
                for (auto j = 0; j < dom_dim_; j++)
                {
                    if (j == 0 || all_knot_levels[j][anchor[j]] > anchor_max_level)
                        anchor_max_level = all_knot_levels[j][anchor[j]];
                }

                // set the destination tensor for the point
                if (anchor_max_level <= tensor_prods[tensor_idx].level)
                {
                    tensor_tot_nctrl_pts[tensor_idx]++;
                    tensor_idxs[vol_iter.cur_iter()].push_back(tensor_idx);

                    // debug
//                     cerr << "ctrl_pt(" << vol_iter.cur_iter() << ") ijk [ " << ijk.transpose() << " ] value " <<
//                         ctrl_pts.row(vol_iter.cur_iter()) << " destination tensor " << tensor_idx << endl;
                }

                // if degree is odd, check borders of neighboring tensors
                // because an anchor can lie on a line between tensors, in which case
                // the control point is copied in multiple tensors
                for (auto j = 0; j < dom_dim_; j++)
                {
                    if (p_(j) % 2)              // odd degree
                    {
                        vector<int> neighbor_idxs;
                        in_neighbors(anchor, j, tensor_idx, neighbor_idxs);
                        for (auto k = 0; k < neighbor_idxs.size(); k++)
                        {
                            TensorIdx           neigh_idx       = neighbor_idxs[k];
                            TensorProduct<T>&   neigh_tensor    = tensor_prods[neigh_idx];
                            if (anchor_max_level <= neigh_tensor.level && in(anchor, neigh_tensor, true))
                            {
                                tensor_tot_nctrl_pts[neigh_idx]++;
                                tensor_idxs[vol_iter.cur_iter()].push_back(neigh_idx);

                                // debug
//                                 cerr << "ctrl_pt(" << vol_iter.cur_iter() << ") ijk [ " << ijk.transpose() << " ] value " <<
//                                     ctrl_pts.row(vol_iter.cur_iter()) << " destination tensor " << neigh_idx << endl;
                            }
                        }   // neighbors
                    }   // odd degree
                }   // j < dom_dim_

                anchor[0]++;
                vol_iter.incr_iter();                       // increment vol_iter

                // update tensor_idx and start_tensor_idx, dealing with boundary conditions
                for (auto k = 0; k < dom_dim_; k++)
                {
                    if (k < dom_dim_ - 1 && vol_iter.done(k))
                    {
                        anchor[0] = (p_(0) + 1) / 2;
                        anchor[k + 1]++;

                        // debug
//                         cerr << "2: anchor [ " << anchor[0] << " " << anchor[1] << " ]" << endl;

                        if (ijk(k + 1) < nctrl_pts(k + 1))
                        {
                            // check for the anchor in the current tensor and in next pointers in next higher dim, starting back at last tensor of current dim
                            tensor_idx = in_and_next(anchor, start_tensor_idx[k + 1], k + 1, true);
                            if (tensor_idx < tensor_prods.size())
                            {
                                // TODO: following is untested, need higher dimension example with multiple tensors
                                start_tensor_idx[k + 1] = tensor_idx;           // adjust start tensor of next dim
                                start_tensor_idx[k]     = start_tensor_idx[0];  // reset start tensor of current dim
                            }
                        }
                    }
                    if (!vol_iter.done(k))
                    {
                        // debug
//                         if (anchor[0] == 4 && anchor[1] == 1)
//                             cerr << "0: anchor [ " << anchor[0] << " " << anchor[1] << " ] " << "tensor_idx = " << tensor_idx << endl;

                        tensor_idx = in_and_next(anchor, tensor_idx, 0, true);

                        // debug
//                         if (anchor[0] == 4 && anchor[1] == 1)
//                             cerr << "1: anchor [ " << anchor[0] << " " << anchor[1] << " ] " << "tensor_idx = " << tensor_idx << endl;
                    }
                }
            }       // vol_iter not done

            // debug: assert that total number of control points in each tensor matches
            // allocated space and product of nctrl_pts in the tensor
            for (int i = 0; i < tensor_prods.size(); i++)
            {
                // debug
                if (tensor_tot_nctrl_pts[i] != tensor_prods[i].ctrl_pts.rows())
                    fprintf(stderr, "1:\n");

                // debug
                assert(tensor_tot_nctrl_pts[i] == tensor_prods[i].ctrl_pts.rows());
                assert(tensor_tot_nctrl_pts[i] == tensor_prods[i].weights.size());
                assert(tensor_tot_nctrl_pts[i] == tensor_prods[i].nctrl_pts.prod());
            }

            // copy control points and weights to tensors
            // their size should be correct already because they were resized elsewhere in append_tensor()
            for (int i = 0; i < ctrl_pts.rows(); i++)
            {
                for (auto j = 0; j < tensor_idxs[i].size(); j++)
                {
                    // don't lose the MFA_NAW label on control points by overwriting it
                    if (tensor_prods[tensor_idxs[i][j]].weights(tensor_cur_nctrl_pts[tensor_idxs[i][j]]) != MFA_NAW)
                    {
                        tensor_prods[tensor_idxs[i][j]].ctrl_pts.row(tensor_cur_nctrl_pts[tensor_idxs[i][j]]) = ctrl_pts.row(i);
                        tensor_prods[tensor_idxs[i][j]].weights(tensor_cur_nctrl_pts[tensor_idxs[i][j]])      = weights(i);
                    }
                    tensor_cur_nctrl_pts[tensor_idxs[i][j]]++;
                }
            }
        }

        // copy subset of control points into a given tensor product in the tmesh
        // assumes that the destination tensor is the most refined, ie, no knots or control points in its interior should be skipped
        void subset_ctrl_pts(const VectorXi&        nctrl_pts,          // number of control points in each dim.
                             const MatrixX<T>&      ctrl_pts,           // control points
                             const VectorX<T>&      weights,            // weights
                             int                    tensor_idx,         // index of destination tensor
                             int                    parent_tensor_idx)  // index of parent tensor where new control points originated
        {
            TensorProduct<T>& t     = tensor_prods[tensor_idx];         // destination (child) tensor
            TensorProduct<T>& pt    = tensor_prods[parent_tensor_idx];  // parent tensor

            // get starting offsets and numbers of control points in the subset
            VectorXi sub_starts(dom_dim_);
            t.nctrl_pts.resize(dom_dim_);
            for (auto i = 0; i < dom_dim_; i++)
            {
                if (t.knot_maxs[i] == pt.knot_mins[i])      // child tensor is to the min size of parent
                    sub_starts(i) = 0;
                else
                {
                    if (p_(i) % 2 == 0)
                        sub_starts(i) = t.knot_mins[i] - pt.knot_mins[i] - 1;
                    else
                        sub_starts(i) = t.knot_mins[i] - pt.knot_mins[i] - 2;
                }
                if (p_(i) % 2 == 0)
                    t.nctrl_pts(i) = t.knot_maxs[i] - t.knot_mins[i];
                else
                    t.nctrl_pts(i) = t.knot_maxs[i] - t.knot_mins[i] + 1;
            }

            // allocate control points
            t.ctrl_pts.resize(t.nctrl_pts.prod(), tensor_prods[0].ctrl_pts.cols());
            t.weights.resize(t.ctrl_pts.rows());

            // copy control points
            VolIterator vol_iter(t.nctrl_pts, sub_starts, nctrl_pts);
            VectorXi ijk(dom_dim_);
            while (!vol_iter.done())
            {
                vol_iter.idx_ijk(vol_iter.cur_iter(), ijk);
                t.ctrl_pts.row(vol_iter.cur_iter()) = ctrl_pts.row(vol_iter.ijk_idx(ijk));
                t.weights(vol_iter.cur_iter()) = weights(vol_iter.ijk_idx(ijk));
                vol_iter.incr_iter();
            }
        }

        // check tensor and next pointers of tensor looking for a tensor containing the point
        // only checks the direct next neighbors, not multiple hops
        // returns index of tensor containing the point, or size of tensors (end) if not found
        TensorIdx in_and_next(
                        const vector<KnotIdx>&  pt,                     // target point in index space
                        int                     tensor_idx,             // index of starting tensor for the walk
                        int                     cur_dim,                // dimension in which to walk
                        bool                    ctrl_pt_anchor) const   // whether pt refers to control point anchor (shifted 1/2 space for even degree)
        {
            const TensorProduct<T>& tensor = tensor_prods[tensor_idx];

            // check current tensor
            if (in(pt, tensor, ctrl_pt_anchor))
                return tensor_idx;

            // check nearest neighbor next tensors
            for (auto i = 0; i < tensor.next[cur_dim].size(); ++i)
            {
                if (in(pt, tensor_prods[tensor.next[cur_dim][i]], ctrl_pt_anchor))
                    return tensor.next[cur_dim][i];
            }
            return tensor_prods.size();
        }

        // check tensor and prev and next pointers of tensor looking for a tensor containing the point
        // only checks the direct prev and next neighbors, not multiple hops
        // returns index of tensor containing the point, or size of tensors (end) if not found
        TensorIdx in_prev_next(
                const vector<KnotIdx>&  pt,                     // target point in index space
                int                     tensor_idx,             // index of starting tensor for the walk
                int                     cur_dim,                // dimension in which to walk
                bool                    ctrl_pt_anchor) const   // whether pt refers to control point anchor (shifted 1/2 space for even degree)
        {
            const TensorProduct<T>& tensor = tensor_prods[tensor_idx];

            // check current tensor
            if (in(pt, tensor, ctrl_pt_anchor))
                return tensor_idx;

            // check nearest neighbor prev tensors
            for (auto i = 0; i < tensor.prev[cur_dim].size(); ++i)
            {
                if (in(pt, tensor_prods[tensor.prev[cur_dim][i]], ctrl_pt_anchor))
                    return tensor.prev[cur_dim][i];
            }

            // check nearest neighbor next tensors
            for (auto i = 0; i < tensor.next[cur_dim].size(); ++i)
            {
                if (in(pt, tensor_prods[tensor.next[cur_dim][i]], ctrl_pt_anchor))
                    return tensor.next[cur_dim][i];
            }

            return tensor_prods.size();
        }

        // search for tensor containing point in index space
        // returns index of tensor containing the point, or size of tensors (end) if not found
        TensorIdx search_tensors(
                const vector<KnotIdx>&   pt,                                // target point in index space
                const VectorXi&          pad)                               // padding in each dim. between target and extents of tensor, zero size -> unused
        {
            for (auto i = 0; i < tensor_prods.size(); i++)                  // for all existing tensors
            {
                TensorProduct<T>& t = tensor_prods[i];

                int j;
                for (j = 0; j < dom_dim_; j++)
                {
                    // point falls into the mins, maxs of this tensor, within the pad
                    int p = pad.size() == 0 ? 0 : pad(j);
                    if (pt[j] - p < t.knot_mins[j] || pt[j] + p > t.knot_maxs[j])
                        break;
                }

                if (j == dom_dim_)
                    return i;
            }   // for all existing tensors

            return tensor_prods.size();
        }

        // convert (i,j,k,...) multidimensional index into linear index into domain
        // number of dimension is the domain dimensionality
        void ijk2idx(
                const VectorXi& ndom_pts,               // number of input points in each dimension
                const VectorXi& ijk,                    // i,j,k,... indices to all dimensions
                size_t&         idx) const              // (output) linear index
        {
            idx           = 0;
            size_t stride = 1;
            for (int i = 0; i < dom_dim_; i++)
            {
                idx += ijk(i) * stride;
                stride *= ndom_pts(i);
            }
        }

        // determine starting and ending indices of domain input points covered by one tensor product
        // coverage extends to edge of basis functions corresponding to control points in the tensor product
        void domain_pts(TensorIdx               t_idx,              // index of current tensor product
                        vector<vector<T>>&      params,             // params of input points
                        vector<size_t>&         start_idxs,         // (output) starting idxs of input points
                        vector<size_t>&         end_idxs) const     // (output) ending idxs of input points
        {
            // debug
            bool debug = false;
//             if (t_idx == 1)
//                 debug = true;

            start_idxs.resize(dom_dim_);
            end_idxs.resize(dom_dim_);
            vector<KnotIdx> min_anchor(dom_dim_);                   // anchor for the min. edge basis functions of the new tensor
            vector<KnotIdx> max_anchor(dom_dim_);                   // anchor for the mas. edge basis functions of the new tensor
            vector<vector<KnotIdx>> local_knot_idxs;                // local knot vector for an anchor

            const TensorProduct<T>& tc = tensor_prods[t_idx];

            // left edge
            for (auto k = 0; k < dom_dim_; k++)
                min_anchor[k] = tc.knot_mins[k];
            knot_intersections(min_anchor, t_idx, true, local_knot_idxs);
            vector<KnotIdx> start_knot_idxs(dom_dim_);
            for (auto k = 0; k < dom_dim_; k++)
                start_knot_idxs[k] = local_knot_idxs[k][1];                             // one knot away from the front

            // right edge
            local_knot_idxs.clear();
            for (auto k = 0; k < dom_dim_; k++)
            {
                if (p_(k) % 2 == 0)
                    max_anchor[k] = tc.knot_maxs[k] - 1;
                else
                    max_anchor[k] = tc.knot_maxs[k];
            }
            knot_intersections(max_anchor, t_idx, true, local_knot_idxs);
            vector<KnotIdx> end_knot_idxs(dom_dim_);
            for (auto k = 0; k < dom_dim_; k++)
                end_knot_idxs[k] = local_knot_idxs[k][local_knot_idxs[k].size() - 2];   // one knot away from the back

            // input points corresponding to start and end knot values
            for (auto k = 0; k < dom_dim_; k++)
            {
                // start points begin at all_knot_param_idxs[start_knot_idxs]
                start_idxs[k]   = all_knot_param_idxs[k][start_knot_idxs[k]];

                // end points go up to but do not include all_knot_param_ixs[end_knot_idxs + 1]
                if (end_knot_idxs[k] < all_knots[k].size() - 1)
                    end_idxs[k] = all_knot_param_idxs[k][end_knot_idxs[k] + 1] - 1;
                else
                    end_idxs[k] = all_knot_param_idxs[k][end_knot_idxs[k]];
            }

            // debug
//             if (debug)
//                 fmt::print(stderr, "start_knot_idxs [{}] end_knot_idxs [{}] start_pt_idxs [{}] end_pt_idxs [{}]\n",
//                         fmt::join(start_knot_idxs, ","), fmt::join(end_knot_idxs, ","), fmt::join(start_idxs, ","), fmt::join(end_idxs, ","));
        }

        // for a given tensor, get anchor of control point, given control point multidim index
        // anchor is in global knot index space (includes knots at higher refinement levels than the tensor)
        void ctrl_pt_anchor(const TensorProduct<T>& t,              // tensor product
                            const VectorXi&         ijk,            // multidim index of control point
                            vector<KnotIdx>&        anchor) const   // (output) anchor
        {
            for (auto j = 0; j < dom_dim_; j++)
            {
                anchor[j] = ijk(j) + t.knot_mins[j];                // add knot_mins to get from local (in this tensor) to global (in the t-mesh) anchor
                if (t.knot_mins[j] == 0)
                    anchor[j] += (p_(j) + 1) / 2;                   // first control point has anchor floor((p + 1) / 2)
                // check for any knots at a higher level of refinement that would add to the anchor index (anchor is global over all knots)
                for (auto i = t.knot_mins[j]; i <= t.knot_maxs[j]; i++)
                {
                    if (all_knot_levels[j][i] > t.level && anchor[j] >= i)
                        anchor[j]++;
                }
            }
        }

        // offsets a knot index by some amount within a tensor, skipping over any knots at a deeper level
        // returns whether the full offset was achieved (true) or whether ran out of tensor bounds (false)
        // if the tensor ran out of bounds, computes as much offset as possible, ie, ofst_idx = the min or max bound
        bool knot_idx_ofst(
                const TensorProduct<T>& t,                          // tensor product
                KnotIdx                 orig_idx,                   // starting knot idx
                int                     ofst,                       // affset amount, can be positive or negative
                int                     cur_dim,                    // current dimension
                bool                    edge_check,                 // check for missing control points at global edge
                KnotIdx&                ofst_idx) const             // (output) offset knot idx
        {
            ofst_idx    = orig_idx;
            int sgn     = (0 < ofst) - (ofst < 0);                  // sgn = 1 for positive ofst, -1 for negative, 0 for zero
            int p       = p_(cur_dim);                              // degree in current dimension
            int pad     = edge_check ? p - 1 : 0;

            // t is completely to the right of orig_idx and we're offsetting left
            // the offsetted point cannpt be inside of t
            if (sgn == -1 && t.knot_mins[cur_dim] >= orig_idx)
            {
                ofst_idx = pad;
                return false;
            }

            // t is completely to the left of orig_idx and we're offsetting right
            // the offsetted point cannpt be inside of t
            if (sgn == 1 && t.knot_maxs[cur_dim] <= orig_idx)
            {
                ofst_idx  = all_knots[cur_dim].size() - 1 - pad;
                return false;
            }

            // the offsetted point can be inside of t
            for (auto i = 0; i < abs(ofst); i++)
            {
                while ((long)ofst_idx + sgn >= t.knot_mins[cur_dim]   &&
                        (long)ofst_idx + sgn <= t.knot_maxs[cur_dim]  &&
                        all_knot_levels[cur_dim][ofst_idx + sgn] > t.level)
                    ofst_idx += sgn;
                if (t.knot_mins[cur_dim] == 0 &&
                        (long)ofst_idx + sgn < pad)                           // missing control points at global min edge
                {
                    ofst_idx = pad;
                    return false;
                }
                if (t.knot_maxs[cur_dim] == all_knots[cur_dim].size() - 1 &&
                        (long)ofst_idx + sgn > all_knots[cur_dim].size() - 1 - pad)   // missing control points at global max edge
                {
                    ofst_idx  = all_knots[cur_dim].size() - 1 - pad;
                    return false;
                }
                if ((long)ofst_idx + sgn < t.knot_mins[cur_dim])
                {
                    ofst_idx = t.knot_mins[cur_dim];
                    return false;
                }
                if ((long)ofst_idx + sgn > t.knot_maxs[cur_dim])
                {
                    ofst_idx = t.knot_maxs[cur_dim];
                    return false;
                }
                ofst_idx += sgn;
            }
            return true;
        }

        // counts number of knot indices between min and max index skipping knots at a deeper level than current tensor
        KnotIdx knot_idx_dist(
                const TensorProduct<T>& t,                          // tensor product
                KnotIdx                 min,                        // min knot idx
                KnotIdx                 max,                        // max knot idx
                int                     cur_dim,                    // current dimension
                bool                    inclusive) const            // whether to include max
        {
            KnotIdx dist    = 0;
            KnotIdx end     = inclusive ? max + 1 : max;
            for (auto idx = min; idx < end; idx++)
            {
                while (idx < end && all_knot_levels[cur_dim][idx] > t.level)
                    idx++;
                if (idx < end)
                    dist++;
            }
            return dist;
        }

        // binary search to find the span in the knots vector containing a given parameter value
        // returns span index i s.t. u is in [ knots[i], knots[i + 1] )
        // NB closed interval at left and open interval at right
        //
        // i will be in the range [p, n], where n = number of control points - 1 because there are
        // p + 1 repeated knots at start and end of knot vector
        // algorithm 2.1, P&T, p. 68
        int FindSpan(
                int                     cur_dim,            // current dimension
                T                       u) const            // parameter value
        {
            int nctrl_pts = all_knots[cur_dim].size() - p_(cur_dim) - 1;

            if (u == all_knots[cur_dim][nctrl_pts])
                return nctrl_pts - 1;

            // binary search
            int low = p_(cur_dim);
            int high = nctrl_pts;
            int mid = (low + high) / 2;
            while (u < all_knots[cur_dim][mid] || u >= all_knots[cur_dim][mid + 1])
            {
                if (u < all_knots[cur_dim][mid])
                    high = mid;
                else
                    low = mid;
                mid = (low + high) / 2;
            }

            // debug
//             cerr << "u = " << u << " span = " << mid << endl;

            return mid;
        }

        // updates the vectors of knots belonging to this tensor
        // assumes knot_mins and knot_maxs are up to date, only considering knots in that range
        void tensor_knot_idxs(TensorProduct<T>& t)
        {
            t.knot_idxs.resize(dom_dim_);

            for (auto k = 0; k < dom_dim_; k++)
            {
                // walk the knots, copying relevant indices
                t.knot_idxs[k].clear();
                for (auto i = t.knot_mins[k]; i <= t.knot_maxs[k]; i++)
                {
                    if (i == t.knot_mins[k] || i == t.knot_maxs[k] || all_knot_levels[k][i] <= t.level)
                        t.knot_idxs[k].push_back(i);
                }
            }
        }

        // check number of knots belonging to this tensor against the number of control points (for debugging)
        bool check_num_knots_ctrl_pts(TensorIdx tidx)
        {
            auto& t = tensor_prods[tidx];
            for (auto i = 0; i < dom_dim_; i++)
            {
                int nctrl = t.knot_idxs[i].size() - 1;
                if (p_(i) % 2)                                  // odd degree
                    nctrl++;
                if (t.knot_mins[i] == 0)                        // min. edge of global domain
                    nctrl -= (p_(i) + 1) / 2;
                if (t.knot_maxs[i] == all_knots[i].size() - 1)  // max. edge of global domain
                    nctrl -= (p_(i) + 1) / 2;

                if (nctrl != t.nctrl_pts(i))
                {
                    fmt::print(stderr, "Error: check_num_knots_ctrl_pts(): Number of knots and control points in tensor {} in dim. {} do not agree.\n",
                            tidx, i);
                    print_tensor(t, true, false, false);
                    abort();
                }
            }
            return true;
        }

        // check number of knots belonging to this tensor is at least degree + extra
        // returns true if the check passes
        bool check_num_knots_degree(TensorProduct<T>&   t,
                                    int                 extra)
        {
            for (auto k = 0; k < dom_dim_; k++)
            {
                KnotIdx dist = knot_idx_dist(t, t.knot_mins[k], t.knot_maxs[k], k, false);
                if (p_(k) % 2 == 0 && dist < p_(k) + extra || p_(k) % 2 == 1 && dist <  p_(k) + extra - 1)
                    return false;
            }
            return true;
        }

        // check number of control points belonging to this tensor is at least degree + extra
        // returns true if the check passes
        bool check_num_ctrl_degree(TensorIdx            tidx,
                                    int                 extra)
        {
            auto& t = tensor_prods[tidx];
            for (auto j = 0; j < dom_dim_; j++)
            {
                if (t.nctrl_pts(j) < p_(j) + extra)
                {
                    fmt::print(stderr, "Error: one of the tensors has fewer than p + {} control points. This should not happen\n", extra);
                    fmt::print(stderr, "Existing tensor tidx {} knot_mins [{}] knot_maxs[{}] level {}\n",
                            fmt::join(t.knot_mins, ","), fmt::join(t.knot_maxs, ","), t.level);
                    return false;
                }
            }
            return true;
        }

        void print_tensor(
                const TensorProduct<T>&     t,
                bool                        print_knots     = false,
                bool                        print_ctrl_pts  = false,
                bool                        print_weights   = false) const
        {
                fprintf(stderr, "knot [mins] : [maxs] [ ");
                for (int i = 0; i < dom_dim_; i++)
                    fprintf(stderr,"%lu ", t.knot_mins[i]);
                fprintf(stderr, "] : ");

                fprintf(stderr, "[ ");
                for (int i = 0; i < dom_dim_; i++)
                    fprintf(stderr,"%lu ", t.knot_maxs[i]);
                fprintf(stderr, "]\n");

                fprintf(stderr, "nctrl_pts [ ");
                for (int i = 0; i < dom_dim_; i++)
                    fprintf(stderr,"%d ", t.nctrl_pts[i]);
                fprintf(stderr, "]\n");

                fprintf(stderr, "n_local_knots [ ");
                for (int i = 0; i < dom_dim_; i++)
                    fprintf(stderr,"%lu ", t.knot_idxs[i].size());
                fprintf(stderr, "]\n");

                fprintf(stderr, "next tensors [ ");
                for (int i = 0; i < dom_dim_; i++)
                {
                    fprintf(stderr, "[ ");
                    for (const TensorIdx& n : t.next[i])
                        fprintf(stderr, "%lu ", n);
                    fprintf(stderr, "] ");
                    fprintf(stderr," ");
                }
                fprintf(stderr, "]\n");

                fprintf(stderr, "previous tensors [ ");
                for (int i = 0; i < dom_dim_; i++)
                {
                    fprintf(stderr, "[ ");
                    for (const TensorIdx& n : t.prev[i])
                        fprintf(stderr, "%lu ", n);
                    fprintf(stderr, "] ");
                    fprintf(stderr," ");
                }

                fprintf(stderr, "]\n\n");

                if (print_knots)
                {
                    for (int i = 0; i < dom_dim_; i++)
                    {
                        fprintf(stderr, "knots[dim %d]\n", i);
                        for (auto j = 0; j < t.knot_idxs[i].size(); j++)
                        {
                            KnotIdx idx = t.knot_idxs[i][j];
                            fprintf(stderr, "idx %lu: %.4lf (l %d) [p %lu]\n",
                                    idx, all_knots[i][idx], all_knot_levels[i][idx], all_knot_param_idxs[i][idx]);
                        }
                        fprintf(stderr, "\n");
                    }
                }

                if (print_ctrl_pts)
                    cerr << "ctrl_pts:\n" << t.ctrl_pts << endl;

                if (print_weights)
                cerr << "weights:\n" << t.weights << endl;

                if (!print_knots)
                    fprintf(stderr, "\n");
        }

        void print_tensors(
                bool print_knots    = false,
                bool print_ctrl_pts = false,
                bool print_weights  = false) const
        {
            for (auto j = 0; j < tensor_prods.size(); j++)
            {
                const TensorProduct<T>& t = tensor_prods[j];
                if (j == 0)
                    fmt::print(stderr, "-----\n\n");
                fmt::print(stderr, "tensor_prods[{}] level={} done={}\n", j, t.level, t.done);
                print_tensor(t, print_knots, print_ctrl_pts, print_weights);
                fmt::print(stderr, "-----\n\n");
            }
        }

        void print_knots() const
        {
            for (int i = 0; i < dom_dim_; i++)
            {
                fprintf(stderr, "all_knots[dim %d]\n", i);
                for (auto j = 0; j < all_knots[i].size(); j++)
                    fprintf(stderr, "%d: %.4lf (l %d) [p %lu]\n",
                            j, all_knots[i][j], all_knot_levels[i][j], all_knot_param_idxs[i][j]);
                fprintf(stderr, "\n");
            }
        }

        void print(
                bool print_all_knots    = false,
                bool print_local_knots  = false,
                bool print_ctrl_pts     = false,
                bool print_weights      = false) const
        {
            if (print_all_knots)
            {
                print_knots();
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "T-mesh has %lu tensor products\n\n", tensor_prods.size());
            print_tensors(print_local_knots, print_ctrl_pts, print_weights);
            fprintf(stderr, "\n");
        }

        // debug: check that knots are nondecreasing
        // returns true if knots are ordered correctly
        bool check_knots_order()
        {
            for (auto i = 0; i < dom_dim_; i++)
            {
                for (auto j = 0; j < all_knots[i].size() - 1; j++)
                {
                    if (all_knots[i][j] > all_knots[i][j + 1])
                        return false;
                }
            }
            return true;
        }
    };
}

#endif
