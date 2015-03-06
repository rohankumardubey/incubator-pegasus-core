#include "prepare_list.h"
#include "mutation.h"

#define __TITLE__ "prepare_list"

namespace rdsn { namespace replication {

prepare_list::prepare_list(
        decree initDecree, int maxCount,
        mutation_committer committer)
        : mutation_cache(initDecree, maxCount)
{
    _committer = committer;
    _lastCommittedDecree = 0;
}

void prepare_list::sanity_check()
{
    rdsn_assert (
        last_committed_decree() <= min_decree(), ""
        );
}

void prepare_list::reset(decree initDecree)
{
    _lastCommittedDecree = initDecree;
    mutation_cache::reset(initDecree, true);
}

void prepare_list::truncate(decree initDecree)
{
    while (min_decree() <= initDecree && count() > 0)
    {
        pop_min();
    }
    _lastCommittedDecree = initDecree;
}

int prepare_list::prepare(mutation_ptr& mu, partition_status status)
{
    rdsn_assert(mu->data.header.decree > last_committed_decree(), "");

    int err;
    switch (status)
    {
    case PS_PRIMARY:
        return mutation_cache::put(mu);

    case PS_SECONDARY: 
        commit(mu->data.header.lastCommittedDecree, true);
        err = mutation_cache::put(mu);
        rdsn_assert(err == ERR_SUCCESS, "");
        return err;

    case PS_POTENTIAL_SECONDARY:
        while (true)
        {
            err = mutation_cache::put(mu);
            if (err == ERR_CAPACITY_EXCEEDED)
            {
                rdsn_assert (min_decree() == last_committed_decree() + 1, "");
                rdsn_assert (mu->data.header.lastCommittedDecree > last_committed_decree(), "");
                commit (last_committed_decree() + 1, true);
            }
            else
                break;
        }
        rdsn_assert(err == ERR_SUCCESS, "");
        return err;
     
    case PS_INACTIVE: // only possible during init  
        err = ERR_SUCCESS;
        if (mu->data.header.lastCommittedDecree > max_decree())
        {
            reset(mu->data.header.lastCommittedDecree);
        }
        else if (mu->data.header.lastCommittedDecree > _lastCommittedDecree)
        {
            for (decree d = last_committed_decree() + 1; d <= mu->data.header.lastCommittedDecree; d++)
            {
                _lastCommittedDecree++;   
                if (count() == 0)
                    break;
                
                if (d == min_decree())
                {
                    mutation_ptr mu2 = get_mutation_by_decree(d);
                    pop_min();
                    if (mu2 != nullptr) _committer(mu2);
                }
            }

            rdsn_assert (_lastCommittedDecree == mu->data.header.lastCommittedDecree, "");
            sanity_check();
        }
        
        err = mutation_cache::put(mu);
        rdsn_assert (err == ERR_SUCCESS, "");
        return err;

    default:
        rdsn_assert (false, "");
        return 0;
    }
}

//
// ordered commit
//
bool prepare_list::commit(decree d, bool force)
{
    if (d <= last_committed_decree())
        return false;

    if (!force)
    {
        if (d != last_committed_decree() + 1)
            return false;

        mutation_ptr mu = get_mutation_by_decree(last_committed_decree() + 1);

        while (mu != nullptr && mu->is_ready_for_commit())
        {
            _lastCommittedDecree++;
            _committer(mu);

            rdsn_assert(mutation_cache::min_decree() == _lastCommittedDecree, "");
            pop_min();

            mu = mutation_cache::get_mutation_by_decree(_lastCommittedDecree + 1);
        }
    }
    else
    {
        for (decree d0 = last_committed_decree() + 1; d0 <= d; d0++)
        {
            mutation_ptr mu = get_mutation_by_decree(d0);
            rdsn_assert(mu != nullptr && mu->is_prepared(), "");

            _lastCommittedDecree++;
            _committer(mu);

            rdsn_assert (mutation_cache::min_decree() == _lastCommittedDecree, "");
            pop_min();
        }
    }

    sanity_check();
    return true;
}

}} // namespace end
