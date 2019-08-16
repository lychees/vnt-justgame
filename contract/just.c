#include "vntlib.h"
#include "payout"

void payout::init() {
    require_auth(_self);
    auto g = _global.get_or_create( _self, global_info{});
    g.st = 1544788800; // 2018/12/14 20:00:00
    g.ed = 1544788800 + 30*24*60*60;
    _global.set(g, _self);
}

void payout::make_profit(uint64_t delta) {
    auto g = _global.get();
    g.earnings_per_share += MAGNITUDE * delta / g.total_staked.amount;
    _global.set(g, _self);
}


void payout::stake(name from, asset delta) {
    require_auth(from);
    // eosio_assert(delta.amount > 0, "must stake a positive amount");
    singleton_players _players(_self, from.value);
    auto v = _players.get_or_create(_self, player_info{});
    auto g = _global.get();
    v.staked += delta;
    v.payout += g.earnings_per_share * delta.amount / MAGNITUDE;
    _players.set(v, _self);
    g.total_staked += delta;
    _global.set(g, _self);
}

void payout::unstake(name from, asset delta) {
    require_auth(from);
    singleton_players _players(_self, from.value);
    auto v = _players.get_or_create(_self, player_info{});
    auto g = _global.get();
    // eosio_assert(delta <= v.staked, "don't have enough token for unstake");
    
    if (g.earnings_per_share * delta.amount / MAGNITUDE <= v.payout) {
        v.payout -= g.earnings_per_share * delta.amount / MAGNITUDE;
    } else {
        v.payout = 0;
    }
    
    v.staked -= delta;
    _players.set(v, _self);
}

void payout::open(name from) {
    require_auth(from);
    claim(from);
    singleton_players _players(_self, from.value);
    auto v = _players.get_or_create(_self, player_info{});
    unstake(from, v.staked);
}

void payout::buy(name from, uint64_t in) {
    uint64_t out = in / 1000;
    check(out > 0, "insufficient box");
    stake(from, asset(out, TOKEN_SYMBOL));
    
    auto g = _global.get_or_create( _self, global_info{});
    g.last = from;
    g.ed += 30;
    
    in *= 90; in /= 100;
    make_profit(in);
    in /= 9;
    g.pool += in;
    _global.set(g, _self);
}

void payout::upgrade(name from) {
    require_auth(from);
    singleton_players _players(_self, from.value);
    auto v = _players.get_or_create(_self, player_info{});
    auto g = _global.get();
    
    // TODO(minakokojima): unvote(v);
    auto delta = asset(0, EOS_SYMBOL);
    auto raw_payout = g.earnings_per_share * v.staked.amount / MAGNITUDE;
    if (raw_payout > v.payout) delta.amount = raw_payout - v.payout;
    
    v.payout = raw_payout;
    _players.set(v, _self);
    
    buy(from, delta.amount > 0);
}

void payout::claim(name from) {
    require_auth(from);
    singleton_players _players(_self, from.value);
    auto v = _players.get_or_create(_self, player_info{});
    auto g = _global.get();
    
    // TODO(minakokojima): unvote(v);
    auto delta = asset(0, EOS_SYMBOL);
    auto raw_payout = g.earnings_per_share * v.staked.amount / MAGNITUDE;
    if (raw_payout > v.payout) delta.amount = raw_payout - v.payout;
    
    v.payout = raw_payout;
    _players.set(v, _self);
    
    if (delta.amount > 0) {
        action(
               permission_level{_self, "active"_n},
               EOS_CONTRACT, "transfer"_n,
               make_tuple(_self, from, delta,
                          string("claim dividend."))
               ).send();
    }
}

void payout::onTransfer(name from, name to, extended_asset in, string memo) {
    
    if (to != _self) return;
    require_auth(from);
    eosio::check(in.quantity.is_valid(), "invalid token transfer");
    eosio::check(in.quantity.is_valid(), "invalid token transfer");
    
    check(in.contract == EOS_CONTRACT, "must use true EOS to make profit");
    check(in.quantity.symbol == EOS_SYMBOL, "must use EOS to make profit");
    // eosio_assert(in.quantity.amount > 0, "must transfer a positive amount");
    
    auto params = split(memo, ' ');
    // eosio_assert(params.size() >= 1, "error memo");
    
    buy(from, in.quantity.amount);
}


typedef struct
{
  uint256 balance;     //存款
  string nickName;     //昵称
  bool freeAddress;    //是否已经领取过赠送的筹码
  uint64 winCount;     //赢的局数
  uint64 loseCount;    //输的局数
  uint64 chickenCount; //猜中50的局数
  uint256 winReward;   //赢的收益
  uint256 loseAmount;  //输的总额
} Account;
//账号余额
KEY mapping(address, Account) accounts;

//总局数
KEY uint64 totalGameCount;

//存款总额
KEY uint256 deposit;

// 10%
KEY uint256 fee = U256(10);

KEY address owner;

KEY uint256 freeAmount = U256(100000000000000000000); // 100*10**18;

EVENT EVENT_BET(indexed address from, string nickname, uint256 amount,
                int32 bigger, uint64 lottery, uint256 reward);
EVENT EVENT_WITHDRAW(indexed address from, string nickname, uint256 amount);
EVENT EVENT_DEPOSIT(indexed address from, string nickname, uint256 amount);
EVENT EVENT_NICKNAME(indexed address from, string nickName);
EVENT EVENT_GETFREEVNT(indexed address from, bool got);

constructor $Dice()
{
  owner = GetSender();
  totalGameCount = 0;
}

// getFee
uint256 getReward(uint256 amount)
{
  PrintUint256T("get amount in getreward:", amount);
  PrintUint256T("get fee1:", fee);
  uint256 res = U256SafeDiv(amount, fee);
  PrintUint256T("get fee2:", res);
  uint256 reward = U256SafeSub(amount, res);
  PrintUint256T("get reward:", reward);
  return reward;
}

//是否有足够的赌注
void checkAmount(uint256 amount)
{
  Require(U256_Cmp(amount, U256(0) == 1), "amount must > 0");
  address from = GetSender();
  accounts.key = from;
  uint256 balance = accounts.value.balance;
  PrintAddress("get sender:", from);
  PrintUint256T("get balance:", balance);
  Require(U256_Cmp(U256SafeSub(balance, amount), 0) != -1,
          "No enough money to bet");
}

//奖池是否足够
void checkPool(uint256 amount)
{
  uint256 contractBalance = GetBalanceFromAddress(GetContractAddress());
  PrintAddress("get contract address:", GetContractAddress());
  PrintUint256T("get contract balance:", contractBalance);
  PrintUint256T("get deposit balance:", deposit);
  uint256 reward = getReward(amount);
  Require(
      U256_Cmp(U256SafeSub(contractBalance,
                           U256SafeAdd(deposit, U256SafeMul(reward, U256(10)))),
               0) != -1,
      "No enough money in prize pool");
}

void checkOwner()
{
  address sender = GetSender();
  Require(Equal(sender, owner) == true, "Only the owner can operate");
}

uint64 random()
{
  uint64 time = GetTimestamp();
  PrintUint64T("get time", time);
  string time_sha3 = SHA3(SHA3(SHA3(FromU64(time))));
  PrintStr("get time sha3", time_sha3);
  uint64 index = time % 63 + 2;
  PrintStr("get index", index);
  uint64 gas = GetGas() % 64 + 2;
  PrintStr("get gas", gas);
  uint64 random_a = (uint64)time_sha3[index];
  PrintUint64T("get random_a", random_a);
  uint64 random_b = (uint64)time_sha3[index + 1];
  PrintUint64T("get random_b", random_b);
  uint64 random_c = random_a * random_b * gas % 101;
  PrintUint64T("get result", random_c);
  return random_c;
}

UNMUTABLE
uint64 testRandom() { return random(); }

//-1:<50,0:=50,1:>50
MUTABLE
void Bet(uint256 amount, int32 bigger)
{
  PrintUint256T("get amount:", amount);
  checkAmount(amount);
  checkPool(amount);
  address sender = GetSender();
  uint64 res = random();
  totalGameCount += 1;
  if (res > 50 && bigger == 1)
  {
    // you win
    accounts.key = sender;
    uint256 reward = getReward(amount);
    accounts.value.balance = U256SafeAdd(accounts.value.balance, reward);
    accounts.value.winReward = U256SafeAdd(accounts.value.winReward, reward);
    deposit = U256SafeAdd(deposit, reward);
    accounts.value.winCount += 1;
    EVENT_BET(sender, accounts.value.nickName, amount, bigger, res, reward);
  }
  else if (res < 50 && bigger == -1)
  {
    // you win
    accounts.key = sender;
    uint256 reward = getReward(amount);
    accounts.value.balance = U256SafeAdd(accounts.value.balance, reward);
    accounts.value.winReward = U256SafeAdd(accounts.value.winReward, reward);
    deposit = U256SafeAdd(deposit, reward);
    accounts.value.winCount += 1;
    EVENT_BET(sender, accounts.value.nickName, amount, bigger, res, reward);
  }
  else if (res == 50 && bigger == 0)
  {
    // you are the luckist man
    accounts.key = sender;
    uint256 reward = getReward(amount);
    reward = U256SafeMul(reward, U256(100));
    accounts.value.balance = U256SafeAdd(accounts.value.balance, reward);
    accounts.value.winReward = U256SafeAdd(accounts.value.winReward, reward);
    deposit = U256SafeAdd(deposit, reward);
    accounts.value.chickenCount += 1;
    EVENT_BET(sender, accounts.value.nickName, amount, bigger, res, reward);
  }
  else
  {
    // you lose
    accounts.key = sender;
    accounts.value.balance = U256SafeSub(accounts.value.balance, amount);
    accounts.value.loseAmount = U256SafeAdd(accounts.value.loseAmount, amount);
    deposit = U256SafeSub(deposit, amount);
    accounts.value.loseCount += 1;
    EVENT_BET(sender, accounts.value.nickName, amount, bigger, res, U256(0));
  }
}

//提款
MUTABLE
void Withdraw(uint256 amount)
{
  checkAmount(amount);
  address from = GetSender();
  if (TransferFromContract(from, amount) == true)
  {

    accounts.key = from;
    accounts.value.balance = U256SafeSub(accounts.value.balance, amount);
    deposit = U256SafeSub(deposit, amount);
    EVENT_WITHDRAW(from, accounts.value.nickName, amount);
  }
}

//提取全部
MUTABLE
void WithdrawAll()
{
  accounts.key = GetSender();
  uint256 amount = accounts.value.balance;
  Withdraw(amount);
}

//提取奖池，only owner
MUTABLE
void WithdrawPool(uint256 amount)
{
  checkOwner();
  checkPool(amount);
  TransferFromContract(GetSender(), amount);
}

//提取奖池
MUTABLE
void WithdrawPoolAll()
{
  uint256 amount = GetBalanceFromAddress(GetContractAddress());
  WithdrawPool(amount);
}

//扩充奖池
MUTABLE
void $DepositPool() {}

//存款
MUTABLE
void $Deposit()
{
  uint256 amount = GetValue();
  address from = GetSender();
  accounts.key = from;
  accounts.value.balance = U256SafeAdd(accounts.value.balance, amount);
  deposit = U256SafeAdd(deposit, amount);
  EVENT_DEPOSIT(from, accounts.value.balance, amount);
}

//免费筹获取100VNT的筹码,每个账号可以获取一次
MUTABLE
void GetFreeChips()
{
  address from = GetSender();
  accounts.key = from;
  bool flag = accounts.value.freeAddress;
  Require(flag == false, "you have got before");
  accounts.value.balance = U256SafeAdd(accounts.value.balance, freeAmount);
  deposit = U256SafeAdd(deposit, freeAmount);
  accounts.value.freeAddress = true;
  EVENT_GETFREEVNT(from, true);
}

MUTABLE
void SetNickName(string name)
{
  address from = GetSender();
  accounts.key = from;
  accounts.value.nickName = name;
  EVENT_NICKNAME(from, name);
}

UNMUTABLE
string GetNickNameFromAddress(address addr)
{
  accounts.key = addr;
  return accounts.value.nickName;
}

UNMUTABLE
string GetNickName() { return GetNickNameFromAddress(GetSender()); }

UNMUTABLE
address GetOwner() { return owner; }

UNMUTABLE
uint256 GetAmountFromAddress(address addr)
{
  accounts.key = addr;
  return accounts.value.balance;
}

UNMUTABLE
uint256 GetAmount() { return GetAmountFromAddress(GetSender()); }

UNMUTABLE
string GetWinAndLose()
{
  accounts.key = GetSender();
  uint64 win = accounts.value.winCount;
  uint64 lose = accounts.value.loseCount;
  uint64 chicken = accounts.value.chickenCount;
  return Concat(
      Concat(Concat(Concat(FromU64(win), "-"), FromU64(chicken)), "-"),
      FromU64(lose));
}

UNMUTABLE
uint256 GetPool()
{
  uint256 amount = GetBalanceFromAddress(GetContractAddress());
  return U256SafeSub(amount, deposit);
}

UNMUTABLE
uint64 GetTotalGameCount() { return totalGameCount; }

$_() { $Deposit(); }
