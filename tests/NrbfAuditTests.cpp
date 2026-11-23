#include "core/NrbfReader.hpp"
#include "core/SaveManager.hpp"
#include "core/CopyTransaction.hpp"
#include "NrbfFixture.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace trainer;
using Bytes = std::vector<std::uint8_t>;
void Check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
Bytes Header() { Bytes b; PushU8(b,0); PushI32(b,1); PushI32(b,-1); PushI32(b,1); PushI32(b,0); return b; }
void String(Bytes& b, int id, std::string_view s) { PushU8(b,6); PushI32(b,id); PushStr(b,s); }
Bytes Scalar(std::uint8_t type, std::int32_t value) {
    auto b=Header(); PushU8(b,4); PushI32(b,1); PushStr(b,"T"); PushI32(b,1);
    PushStr(b,"V"); PushU8(b,0); PushU8(b,type); PushI32(b,value); PushU8(b,11); return b;
}
int main() {
    const auto dir=std::filesystem::temp_directory_path() / ("nrbf-audit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::string error;
        auto reject=[](const Bytes& b) { const auto r=nrbf::Parse(b); Check(!r.ok && !r.error.empty() && r.document.objects.empty(),"Malformed input must fail without partial graph"); };
        const auto valid=BuildValidSavedDataNrbf(72,88,2);
        Check(nrbf::Parse(valid).ok,"Baseline fixture");
        for (std::size_t i=0;i<valid.size();++i) reject(Bytes(valid.begin(),valid.begin()+i));
        auto b=Header(); PushU8(b,11); reject(b); // missing root
        b=Header(); String(b,2,"s"); PushU8(b,11); reject(b);
        b=Header(); String(b,1,"a"); String(b,1,"b"); PushU8(b,11); reject(b);
        b=Header(); String(b,0,"a"); PushU8(b,11); reject(b);
        b=Header(); String(b,1,""); PushU8(b,11); Check(nrbf::Parse(b).ok,"Empty UTF-8 string");
        b=Header(); String(b,1,std::string("\xc0\x80",2)); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,6); PushI32(b,1); for(int i=0;i<5;++i) PushU8(b,255); reject(b);
        b=Header(); PushU8(b,16); PushI32(b,1); PushI32(b,1); PushU8(b,9); PushI32(b,99); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,16); PushI32(b,1); PushI32(b,1); PushU8(b,9); PushI32(b,1); PushU8(b,11);
        Check(nrbf::Parse(b).ok,"Valid cycle remains a non-recursive ID reference");
        b=Header(); PushU8(b,17); PushI32(b,1); PushI32(b,1); PushU8(b,16); PushI32(b,2); PushI32(b,0); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,16); PushI32(b,1); PushI32(b,1); PushU8(b,14); PushI32(b,2); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,16); PushI32(b,1); PushI32(b,1); PushU8(b,13); PushU8(b,0); PushU8(b,11); reject(b);
        // Cumulative null-run expansion, not just a per-array bound.
        b=Header(); for(int id=1;id<=4;++id) { PushU8(b,16); PushI32(b,id); PushI32(b,200000); PushU8(b,14); PushI32(b,200000); } PushU8(b,11); reject(b);
        b=Header(); for(int id=1;id<=70;++id) { PushU8(b,16); PushI32(b,id); PushI32(b,1); } PushU8(b,10); PushU8(b,11); reject(b);
        // Empty array must still validate its element type.
        b=Header(); PushU8(b,7); PushI32(b,1); PushU8(b,0); PushI32(b,1); PushI32(b,0); PushU8(b,0); PushU8(b,255); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,7); PushI32(b,1); PushU8(b,0); PushI32(b,2); PushI32(b,1); PushI32(b,1); PushU8(b,2); PushU8(b,10); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,7); PushI32(b,1); PushU8(b,2); PushI32(b,2); PushI32(b,262144); PushI32(b,262144); reject(b);
        b=Header(); PushU8(b,1); PushI32(b,1); PushI32(b,444); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,12); PushI32(b,2); PushStr(b,"a"); PushU8(b,12); PushI32(b,2); PushStr(b,"b"); String(b,1,"s"); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,5); PushI32(b,1); PushStr(b,"T"); PushI32(b,0); PushI32(b,2); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,4); PushI32(b,1); PushStr(b,"T"); PushI32(b,2); PushStr(b,"x"); PushStr(b,"x"); PushU8(b,0); PushU8(b,0); PushU8(b,8); PushU8(b,8); PushI32(b,1); PushI32(b,2); PushU8(b,11); reject(b);
        // Metadata reuse must not amplify one class name without a global budget.
        b=Header(); PushU8(b,4); PushI32(b,1); PushStr(b,std::string(800000,'T')); PushI32(b,0);
        for(int id=2;id<60;++id) { PushU8(b,1); PushI32(b,id); PushI32(b,1); } PushU8(b,11); reject(b);
        b=Header(); PushU8(b,4); PushI32(b,1); PushStr(b,"T"); PushI32(b,1); PushStr(b,"s"); PushU8(b,1); PushU8(b,13); PushU8(b,1); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,16); PushI32(b,1); PushI32(b,1); PushU8(b,8); PushU8(b,8); PushI32(b,42); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,15); PushI32(b,1); PushI32(b,1); PushU8(b,1); PushU8(b,2); PushU8(b,11); reject(b);
        b=Header(); PushU8(b,7); PushI32(b,1); PushU8(b,3); PushI32(b,1); PushI32(b,1); PushI32(b,INT32_MAX); PushU8(b,2); PushU8(b,10); PushU8(b,11); reject(b);
        auto parsedFixture=nrbf::Parse(valid);
        auto wrongCurrency=valid;
        const auto currencyOffset=parsedFixture.document.objects.at(10).FindField("Count")->byteOffset;
        wrongCurrency[currencyOffset-5]=11; // Single metadata has the same wire width as Int32.
        Check(nrbf::Parse(wrongCurrency).ok && SaveManager::ParseParameters(wrongCurrency).empty(),"Schema rejects a four-byte non-Int32 Count");
        auto wrongLibrary=valid;
        wrongLibrary[currencyOffset-4]=3;
        wrongLibrary.pop_back(); PushU8(wrongLibrary,12); PushI32(wrongLibrary,3); PushStr(wrongLibrary,"OtherAssembly"); PushU8(wrongLibrary,11);
        Check(nrbf::Parse(wrongLibrary).ok && SaveManager::ParseParameters(wrongLibrary).empty(),"Memento type names in another assembly are not trusted");
        auto duplicateKeys=valid;
        const std::string from="currency_Hard[0];", to="currency_Soft[0];";
        auto location=std::search(duplicateKeys.begin(),duplicateKeys.end(),from.begin(),from.end());
        std::copy(to.begin(),to.end(),location);
        Check(SaveManager::ParseParameters(duplicateKeys).empty(),"Duplicate parameter identities reject complete extraction");
        auto scalar=Scalar(8,72); auto parsed=nrbf::Parse(scalar); Check(parsed.ok,"Int32 scalar");
        auto offset=parsed.document.objects.at(1).fields.at(0).byteOffset;
        Check(nrbf::PatchInt32Verified(scalar,{offset,72,500,0,1000},error).has_value(),"Typed Int32 patch");
        auto asChar=Scalar(3,0x008080f0); Check(!nrbf::PatchInt32Verified(asChar,{offset,240,241,0,1000},error),"Four-byte Char must never be treated as Int32");
        auto asSingle=Scalar(11,72); Check(!nrbf::PatchInt32Verified(asSingle,{offset,72,500,0,1000},error),"No patching Single bytes as Int32");
        auto asUint=Scalar(15,72); Check(!nrbf::PatchInt32Verified(asUint,{offset,72,500,0,1000},error),"No patching UInt32 as Int32");
        Check(!nrbf::PatchInt32Verified(scalar,{SIZE_MAX,72,1,0,100},error),"Offset overflow rejected");
        Check(!nrbf::PatchInt32Verified(scalar,{offset,72,1,100,0},error),"Inverted range rejected");
        std::filesystem::create_directories(dir);
        auto put=[](const auto& p,const Bytes& data) { std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(data.data()),data.size()); };
        auto read=[](const auto& p) { std::ifstream f(p,std::ios::binary); return Bytes(std::istreambuf_iterator<char>(f),{}); };
        const auto source=dir/"output.tmp", dest=dir/"output";
        put(source,valid);
        Check(SaveManager::PatchCopyFileVerified(source,dest,"currency_Soft[0];",72,500,error),"Predictable temp alias no longer clobbers source");
        Check(read(source)==valid,"Source preserved when its name is destination.tmp");
        const auto before=read(dest);
        Check(!SaveManager::PatchCopyFileVerified(source,dest,"currency_Soft[0];",72,600,error) && read(dest)==before,"Existing outputs never overwritten");
        const auto hard=dir/"hard"; std::filesystem::create_hard_link(source,hard);
        Check(!SaveManager::PatchCopyFileVerified(source,hard,"currency_Soft[0];",72,600,error) && read(source)==valid,"Hard-link alias rejected");
        std::error_code ec; const auto sym=dir/"symbolic"; std::filesystem::create_symlink(source,sym,ec);
        if (!ec) Check(!SaveManager::PatchCopyFileVerified(source,sym,"currency_Soft[0];",72,600,error) && read(source)==valid,"Symlink alias rejected");
        else std::cout<<"SKIP symlink privilege unavailable\n";
        const auto interrupted=dir/"interrupted";
        for(auto fault : {testing::CopyFault::AfterPartialWrite,testing::CopyFault::BeforePublish}) {
            Check(!testing::PublishNewCopy(interrupted,valid,error,fault) && !std::filesystem::exists(interrupted),"Failure publishes no partial destination");
            Check(read(source)==valid,"Interrupted operation preserves source");
        }
        for(const auto& entry:std::filesystem::directory_iterator(dir)) Check(!entry.path().filename().string().starts_with(".nrbf-stage-"),"Staging cleaned on failures");
        // Deterministic mutation corpus: not a claim of exhaustive fuzzing.
        std::mt19937 random(12345);
        for(int i=0;i<2000;++i) { auto mutation=valid; mutation[random()%mutation.size()]^=static_cast<std::uint8_t>(1+(random()%255)); (void)nrbf::Parse(mutation); }
        std::filesystem::remove_all(dir);
        std::cout<<"NRBF structural, budget, cycle, typed-patch and transaction regressions: PASS\n";
        return 0;
    } catch(const std::exception& ex) { std::cerr<<ex.what()<<'\n'; std::filesystem::remove_all(dir); return 1; }
}
