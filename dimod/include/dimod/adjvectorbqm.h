// Copyright 2020 D-Wave Systems Inc.
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#ifndef DIMOD_ADJVECTORBQM_H_
#define DIMOD_ADJVECTORBQM_H_

#include <algorithm>
#include <utility>
#include <vector>

#include "dimod/utils.h"

namespace dimod {

template<class V, class B>
class AdjVectorBQM {
 public:
    using bias_type = B;
    using variable_type = V;
    using size_type = std::size_t;

    using outvars_iterator = typename std::vector<std::pair<V, B>>::iterator;
    using const_outvars_iterator = typename std::vector<std::pair<V, B>>::const_iterator;

    // in the future we'd probably like to make this protected
    std::vector<std::pair<std::vector<std::pair<V, B>>, B>> adj;

    AdjVectorBQM() {}

    template<class BQM>
    explicit AdjVectorBQM(const BQM &bqm) {
        adj.resize(bqm.num_variables());

        for (variable_type v = 0; v < bqm.num_variables(); ++v) {
            set_linear(v, bqm.get_linear(v));

            auto span = bqm.neighborhood(v);
            adj[v].first.insert(adj[v].first.begin(), span.first, span.second);
        }
    }

    /**
     * Construct a BQM from a dense array.
     *
     * @param dense An array containing the biases. Assumed to contain
     *     `num_variables`^2 elements. The upper and lower triangle are summed.
     * @param num_variables The number of variables. 
     */
    template<class B2>
    AdjVectorBQM(const B2 dense[], size_type num_variables,
                 bool ignore_diagonal = false) {
        // we know how big our linear is going to be
        adj.resize(num_variables);

        bias_type qbias;

        if (!ignore_diagonal) {
            for (size_type v = 0; v < num_variables; ++v) {
                adj[v].second = dense[v*(num_variables+1)];
            }
        }

        for (size_type u = 0; u < num_variables; ++u) {
            for (size_type v = u + 1; v < num_variables; ++v) {
                qbias = dense[u*num_variables+v] + dense[v*num_variables+u];

                if (qbias != 0) {
                    adj[u].first.emplace_back(v, qbias);
                    adj[v].first.emplace_back(u, qbias);
                }
            }
        }
    }

    /// Add one (disconnected) variable to the BQM and return its index.
    variable_type add_variable() {
        adj.resize(adj.size()+1);
        return adj.size()-1;
    }

    /// Get the degree of variable `v`.
    size_type degree(variable_type v) const {
        return adj[v].first.size();
    }

    bias_type get_linear(variable_type v) const {
        return adj[v].second;
    }

    std::pair<bias_type, bool>
    get_quadratic(variable_type u, variable_type v) const {
        assert(u >= 0 && u < adj.size());
        assert(v >= 0 && v < adj.size());
        assert(u != v);

        auto span = neighborhood(u);
        auto low = std::lower_bound(span.first, span.second, v,
                                    utils::comp_v<V, B>);

        if (low == span.second || low->first != v)
            return std::make_pair(0, false);
        return std::make_pair(low->second, true);
    }

    std::pair<outvars_iterator, outvars_iterator>
    neighborhood(variable_type u) {
        assert(u >= 0 && u < invars.size());
        return std::make_pair(adj[u].first.begin(), adj[u].first.end());
    }

    std::pair<const_outvars_iterator, const_outvars_iterator>
    neighborhood(variable_type u) const {
        assert(u >= 0 && u < invars.size());
        return std::make_pair(adj[u].first.cbegin(), adj[u].first.cend());
    }

    size_type num_variables() const {
        return adj.size();
    }

    size_type num_interactions() const {
        size_type count = 0;
        for (auto it = adj.begin(); it != adj.end(); ++it)
            count += it->first.size();
        return count / 2;
    }

    variable_type pop_variable() {
        assert(adj.size() > 0);

        variable_type v = adj.size() - 1;

        // remove v from all of its neighbor's neighborhoods
        for (auto it = adj[v].first.cbegin(); it != adj[v].first.cend(); ++it) {
            auto span = neighborhood(it->first);
            auto low = std::lower_bound(span.first, span.second, v,
                                        utils::comp_v<V, B>);
            adj[it->first].first.erase(low);
        }

        adj.pop_back();

        return adj.size();
    }

    bool remove_interaction(variable_type u, variable_type v) {
        assert(u >= 0 && u < adj.size());
        assert(v >= 0 && v < adj.size());

        auto span = neighborhood(u);
        auto low = std::lower_bound(span.first, span.second, v,
                                    utils::comp_v<V, B>);

        bool exists = !(low == span.second || low->first != v);

        if (exists) {
            adj[u].first.erase(low);

            span = neighborhood(v);
            low = std::lower_bound(span.first, span.second, u,
                                   utils::comp_v<V, B>);

            assert(!(low == span.second || low->first != u) == exists);

            adj[v].first.erase(low);
        }

        return exists;
    }

    void set_linear(variable_type v, bias_type b) {
        assert(v >= 0 && v < invars.size());
        adj[v].second = b;
    }

    bool set_quadratic(variable_type u, variable_type v, bias_type b) {
        assert(u >= 0 && u < adj.size());
        assert(v >= 0 && v < adj.size());
        assert(u != v);

        auto span = neighborhood(u);
        auto low = std::lower_bound(span.first, span.second, v,
                                    utils::comp_v<V, B>);

        bool exists = !(low == span.second || low->first != v);

        if (exists) {
            low->second = b;
        } else {
            adj[u].first.emplace(low, v, b);
        }

        span = neighborhood(v);
        low = std::lower_bound(span.first, span.second, u, utils::comp_v<V, B>);

        assert(!(low == span.second || low->first != u) == exists);

        if (exists) {
            low->second = b;
        } else {
            adj[v].first.emplace(low, u, b);
        }

        // to be consistent with AdjArrayBQM, we return whether the value was
        // set
        return true;
    }
};
}  // namespace dimod

#endif  // DIMOD_ADJVECTORBQM_H_
