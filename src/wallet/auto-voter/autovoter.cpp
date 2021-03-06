//
// Copyright (c) 2017-2020 Project PAI Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//


#include "autovoter.h"
#include "wallet/wallet.h"
#include "validation.h"
#include <numeric>

CAutoVoter::CAutoVoter(CWallet* wallet) :
    configured(false)
{
    pwallet = wallet;
}

CAutoVoter::~CAutoVoter()
{
    stop();
}

void CAutoVoter::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *, bool fInitialDownload)
{
    // we have to wait until the entire blockchain is downloaded
    if (fInitialDownload)
        return;

    if (pwallet == nullptr)
        return;

    // if not configured yet, do nothing
    if (!configured.load())
        return;

    if (!config.autoVote)
        return;

    LOCK2(cs_main, pwallet->cs_wallet);

    const CBlockIndex* tip = chainActive.Tip();

    // check if the new tip is indeed on chain active
    if (pindexNew == nullptr || tip == nullptr || pindexNew->GetBlockHash() != tip->GetBlockHash())
        return;

    const int& blockHeight = tip->nHeight;
    if (blockHeight < Params().GetConsensus().nStakeValidationHeight - 1)
        return;

    const uint256& blockHash = tip->GetBlockHash();
    if (blockHash == uint256())
        return;

    // unlock wallet
    bool shouldRelock = pwallet->IsLocked();
    if (shouldRelock && ! pwallet->Unlock(config.passphrase)) {
        LogPrintf("CAutoVoter: Unlocking wallet: Wrong passphrase\n");
        return;
    }

    // verify each winning ticket in the previous block and
    // if it belongs to the wallet, cast a vote according to the
    // current settings

    std::vector<std::string> voteHashes;
    std::string voteHash;
    CWalletError we;

    for (const uint256& ticketHash : tip->pstakeNode->Winners()) {
        if (!pwallet->IsMyTicket(ticketHash))
            continue;

        std::tie(voteHash, we) = pwallet->Vote(ticketHash, blockHash, blockHeight, config.voteBits, config.extendedVoteBits);

        if (we.code == CWalletError::SUCCESSFUL && !voteHash.empty())
            voteHashes.push_back(voteHash);
        else
            LogPrintf("CAutoVoter: Failed to vote: (%d) %s - (%s)\n", we.code, we.message.c_str(), voteHash.c_str());
    }

    if (shouldRelock) pwallet->Lock();

    if (voteHashes.size() > 0) {
        std::string hashes;
        for (const auto& h : voteHashes) {
            if (hashes.length() > 0)
                hashes += ", ";
            hashes += h;
        }
        LogPrintf("CAutoVoter: Voted: %s\n", hashes.c_str());
    }
}

void CAutoVoter::start()
{
    config.autoVote = true;

    // late initialization, just to ensure that the global are initialized and configured
    if (!configured.load()) {
        config.ParseCommandline();
        ::RegisterValidationInterface(this);
        configured.store(true);
    }
}

void CAutoVoter::stop()
{
    config.autoVote = false;
}

bool CAutoVoter::isStarted() const
{
    return config.autoVote;
}
