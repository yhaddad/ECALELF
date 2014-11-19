### ECALELF side:
git branch splittingForCMSSW
git checkout splittingForCMSSW 
git filter-branch --prune-empty --subdirectory-filter EcalAlCaRecoProducers

mkdir tmpBareRepo/
git init --bare tmpBareReco/
git remote add cmsswMergeRepo tmpBareRepo/
git push cmsswMergeRepo splittingForCMSSW

if [ ! -d "cmssw" ]; then
    mkdir /tmp/cmssw/
    git clone git@github.com:shervin86/cmssw.git
    ln -s cmssw /tmp/cmssw/
fi

mv tmpBareRepo/ cmssw/
cd cmssw/
git remote add -f ecalelf tmpBareRepo/
git merge -s recursive -Xsubtree=Calibration/EcalAlCaRecoProducers --no-commit ecalelf/splittingForCMSSW

exit 0
git checkout master
git branch -d splittingForCMSSW
git remote rm cmsswMergeRepo

rm tmpBareRepo/ -Rf
exit 0
#git push origin splittingForCMSSW

#git subtree split --prefix=EcalAlCaRecoProducers -b split --squash


if [ "${CMSSW_VERSION}" != "" ];then 
git cms-init; 
git remote add -f ecalelf git@github.com:ECALELFS/ECALELF.git
git cms-addpkg Calibration
else
    git clone git@github.com:shervin86/cmssw.git
    cd cmssw
    git remote add -f ecalelf ../tmpBareRepo/
fi

git merge -s recursive -Xsubtree=Calibration/EcalAlCaRecoProducers --no-commit ecalelf/splittingForCMSSW


