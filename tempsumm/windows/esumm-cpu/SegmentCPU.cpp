#include "SegmentCPU.h"
#include "MathOperationsCPU.h"
#include "Results.h"

#include <algorithm>
#include <iostream>

#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <Eigen/IterativeLinearSolvers>

using namespace std;

SegmentCPU::SegmentCPU()
{

}

SegmentCPU::SegmentCPU(string framesDir)
{
	vector<string> frames = FileOperations::listFiles(framesDir, ".jpg");
	//sort(frames.begin(), frames.end());

	cv::TickMeter timeLocal;
	timeLocal.reset(); timeLocal.start();

	Mat frame, grayframe;
	frame = imread(frames[0]);
	frameHistRGB(frame, 256);

	for(int i=1; i<(int)frames.size(); i++)
	{
		//read frame
		frame = imread(frames[i]);

		//compute histogram
		frameHistRGB(frame, 256);

		//compute dissimilarity
		cosineDissimilarity(histograms[i-1], histograms[i]);

		//compute variance
		if(frame.channels() == 3)
			cvtColor(frame, grayframe, CV_RGB2GRAY);
		else if(frame.channels() == 1)
			grayframe = frame;

		frameVariance(grayframe);
	}

	timeLocal.stop();
	Results *result;
	result = Results::getInstance();
	result->setSegmentationParallelPart(timeLocal.getTimeSec());

	dissimilarity[0] = 0;
	variances.insert(variances.begin(),0);


	//find visual effects
	vector<double> effects = findEffects();	

	//find shots
	vector<double> convMat(2,1.0);
	convMat[1] = -1.0;
	vector<double> convEffects = MathOperationsCPU::conv(effects, convMat, "same");

	vector<int> shotsPositions; //conferir se o push back eh 0 ou 1
	if(variances[0] / *max_element(variances.begin(), variances.end()) > 0.1)
		shotsPositions.push_back(0);

	for(int i=0; i<(int)convEffects.size(); i++)
	{
		if(abs(convEffects[i]) == 1.0) 
			shotsPositions.push_back(i);
	}

	for(int i=0; i<(int)shotsPositions.size()-1; i=i+2)
	{
		if(shotsPositions[i+1] - shotsPositions[i] > 10)
		{
			vector<int> startEnd(2);
			startEnd[0] = shotsPositions[i];
			startEnd[1] = shotsPositions[i+1];
			this->shots.push_back(startEnd);
		}
	}

	//filter dissimilarity to find the number of segments present on the video
	vector<double> filter24 = filterDissimilarity(24);
	nscenes = 0;
	for(auto i : filter24)
	{
		if(i > 0)
			nscenes++;
	}
}

void SegmentCPU::frameHistRGB(Mat frame, int bins)
{
	vector<int> hist(bins*3);
	for(int i=0; i<frame.rows; i++){
		for(int j=0; j<frame.cols; j++){
			hist[frame.at<cv::Vec3b>(i,j)[0]+2*bins]++;
			hist[frame.at<cv::Vec3b>(i,j)[1]+bins]++;
			hist[frame.at<cv::Vec3b>(i,j)[2]]++;
		}
	}
	this->histograms.push_back(hist);
}

void SegmentCPU::frameVariance(Mat grayframe)
{
	if(grayframe.channels() == 1)
	{
		Scalar mean, stdDev;
		meanStdDev(grayframe, mean, stdDev);
		double variance = pow(stdDev[0],2);
		variances.push_back(variance);
	}
	else
		variances.push_back(-1);
}

void SegmentCPU::cosineDissimilarity(vector<int> hist1, vector<int> hist2)
{
	// similarity = ( sum (ai x bi) ) / ( sqrt( sum(ai^2) ) x sqrt( sum(bi^2) ) )
	// dissimilarity = 1 - similarity

	double dotProduct = 0, magnitudeH1 = 0, magnitudeH2 = 0;

	for(int i=0; i<(int)hist1.size(); i++)
	{
		dotProduct += (double) hist1[i] * (double) hist2[i];
		magnitudeH1 += pow((double) hist1[i] , 2);
		magnitudeH2 += pow((double) hist2[i] , 2);
	}

	double similarity = dotProduct / (sqrt(magnitudeH1) * sqrt(magnitudeH2));

	dissimilarity.push_back(1-similarity);
}

vector<double> SegmentCPU::filterDissimilarity(int qntNeighbors)
{
	// mv = maximum value of the neighborhood value v
	// if (v > mv) then v = (v - mv) / v
	vector<double> result(dissimilarity.size());

	vector<double> neighborsVec;
	for(int i=0; i<qntNeighbors; i++)
		neighborsVec.push_back(0.0);

	for(int i=0; i<(int)dissimilarity.size(); i++)
		neighborsVec.push_back(dissimilarity[i]);

	for(int i=0; i<qntNeighbors; i++)
		neighborsVec.push_back(0.0);

	int count = 0;
	for(int i=qntNeighbors; i<(int)dissimilarity.size()+qntNeighbors; i++)
	{
		double left = *max_element(neighborsVec.begin()+(i-qntNeighbors), neighborsVec.begin()+i);
		double right = *max_element(neighborsVec.begin()+(i+1), neighborsVec.begin()+(i+qntNeighbors));

		double mv = max(left, right);

		if(neighborsVec[i] > mv)
			result[count] = (neighborsVec[i] - mv) / neighborsVec[i];

		count++;
	}
	return result;
}

vector<vector<int>> SegmentCPU::dissolveDetectionIndexes()
{
	double c = 0.7;
	vector<vector<int>> ind;

	vector<double> mvar = variances;

	double maxV = *max_element(variances.begin(), variances.end());
	for(int i=0; i<(int)variances.size(); i++)
		mvar[i] = mvar[i] / maxV;

	mvar = MathOperationsCPU::medianfilter(mvar,5);

	vector<double> v = smoothSpline(mvar, 0.05);

	vector<double> matConv1(2,1); 
	matConv1[1]=-1;

	vector<double> v1 = MathOperationsCPU::conv(v,matConv1,"same");

	vector<double> matConv2(3,1); 
	matConv2[1]=-2;

	vector<double> v2 = MathOperationsCPU::conv(v,matConv2,"same");

	for(int i=2; i<(int)mvar.size()-2; i++){
		if(v1[i-2] < 0 && v1[i-1] < 0 && v1[i] >= 0 && v1[i+1] > 0 && v1[i+2] > 0){
			int ind_L = i-1; 
			//int minL = 0;--edus
			double minL = 0;//+edus
			int posL = i;
			//int CC = 1;
			//busca no lado esquerdo
			while(ind_L >= 0 && v1[ind_L] < 0){
				if(v2[ind_L] < minL){
					//minL = (int)v2[ind_L];-edus
					minL = v2[ind_L];//+;-edus
					posL = ind_L;
				}
				ind_L--;
			}
			//if(ind_L < 0) break;//-edus
			if(ind_L < -1) break;//+edus

			//busca no lado direito
			int ind_R = i+1;
			//int minR = 0;-edus 
			double minR = 0;//+edus 
			int posR = i;
			while(ind_R < (int)mvar.size() && v1[ind_R] > 0){
				if(v2[ind_R] < minR){
					//minR = (int)v2[ind_R];//-edus
					minR = v2[ind_R];//+edus
					posR = ind_R;
				}
				ind_R++;
			}
			//if(ind_R > mvar.size()) break;--edus
			if(ind_R > (int)mvar.size()+1) break;//+edus


			if(ind_L >= 0 && ind_R < (int)mvar.size() && posR - posL > 10 && v[i] > 0.1){

				double var_center = (posL + posR)/2.0;
				var_center = floor(var_center);
				var_center = v[(int)var_center];
				double var_center_c = (v[posL] + v[posR]) / 4;
				double D_max = abs(var_center - var_center_c);
				double D_max_c = (sqrt(v[posL]) * sqrt(v[posR]) * c) / 2;
				if(D_max < D_max_c){
					vector<int> vaux;
					vaux.push_back(posL);vaux.push_back(posR);
					ind.push_back(vaux);
				}
			}
		}
	}
	return ind;
}

vector<vector<int>> SegmentCPU::mergeIntervals(vector<vector<int>> intervals, int dist)
{
	vector<int> ni;
	vector<int> ne;

	int i = 0, j = 0;
	while(i < (int)intervals.size()-1){

		ni.push_back(intervals[i][0]);

		int acum_int = intervals[i][1]- intervals[i][0];
		while( i < (int)intervals.size()-1 && intervals[i][1] + dist >= intervals[i+1][0]
		&& (acum_int + (intervals[i+1][1] - intervals[i+1][0])) < 90 ){
			acum_int = acum_int + (intervals[i+1][1] - intervals[i+1][0]);
			i++;
		}

		ne.push_back(intervals[i][1]);
		j++;
		i++;
	}

	if(j-1 < 1){
		ni.clear(); ne.clear();
		ni.push_back(intervals[i][0]);
		ne.push_back(intervals[i][1]);
	}

	vector<vector<int> > nIE;
	for(int i=0; i<(int)ni.size(); i++){
		vector<int> aux;
		aux.push_back(ni[i]);
		aux.push_back(ne[i]);
		nIE.push_back(aux);
	}
	return nIE;
}

Eigen::SparseMatrix<double> SegmentCPU::createSparse(vector<vector<int>> mat, vector<int> diags, int rows, int cols)
{
	Eigen::SparseMatrix<double, Eigen::ColMajor> sparse(rows,cols);
	sparse.reserve(Eigen::VectorXi::Constant(cols,3));

	for(int p=0; p<(int)diags.size(); p++)
	{
		int x = 0;
		int y = diags[p];

		for(int xx=0; xx<(int)mat.size(); xx++)
		{
			if(x < rows && y>=0 && y < cols)
				sparse.insert(x,y) = (double)mat[xx][p];
			x++; y++;
		}
	}
	sparse.makeCompressed();
	return sparse;
}

Eigen::SparseMatrix<double> SegmentCPU::createSparse(vector<int> mat, int diags, int rows, int cols)
{
	Eigen::SparseMatrix<double, Eigen::ColMajor> sparse(rows,cols);
	sparse.reserve(Eigen::VectorXi::Constant(cols,3));

	int x = 0;
	int y = diags;

	for(int xx=0; xx<(int)mat.size(); xx++)
	{
		if(x < rows && y>=0)
			sparse.insert(x,y) = (double)mat[xx];
		x++; y++;
	}

	sparse.makeCompressed();
	return sparse;
}

vector<double> SegmentCPU::smoothSpline(vector<double> mvar, double parameter)
{
	int n = mvar.size();

	vector<int> dx(n-1, 1);

	//Q=spdiags([1./dx(1:n-2) -(1./dx(1:n-2)+1./dx(2:n-1)) 1./dx(2:n-1)],0:-1:-2,n,n-2);

	vector<vector<int>> tmpMat(dx.size()-1);
	for(auto& aux:tmpMat)
	{
		aux.push_back(1); aux.push_back(-2); aux.push_back(1);
	}

	vector<int> diags;
	diags.push_back(0); diags.push_back(-1); diags.push_back(-2);

	Eigen::SparseMatrix<double, Eigen::ColMajor> Q = createSparse(tmpMat, diags, n, n-2);

	/*for(int k=0; k<Q.outerSize(); k++)
	{
	for (Eigen::SparseMatrix<double>::InnerIterator it(Q,k); it; ++it)
	{
	cout << "(" << it.row() << "," << it.col() << ") = " << it.value() << endl;
	}
	}*/

	//D=spdiags(d2(:),0,n,n);
	vector<int> d2(n,1);
	Eigen::SparseMatrix<double, Eigen::ColMajor> D = createSparse(d2, 0, n, n);

	//R=spdiags([dx(1:n-2) 2*(dx(1:n-2)+dx(2:n-1)) dx(2:n-1)],-1:1,n-2,n-2);
	vector<vector<int>> tmpMat2(dx.size()-1);
	for(auto& aux:tmpMat2)
	{
		aux.push_back(1); aux.push_back(4); aux.push_back(1);
	}

	vector<int> diags2;
	diags2.push_back(-1); diags2.push_back(0); diags2.push_back(1);
	Eigen::SparseMatrix<double, Eigen::ColMajor> R = createSparse(tmpMat2, diags2, n-2, n-2);


	//QQ=(6*(1-p))*(Q.'*D*Q)+p*R;
	Eigen::SparseMatrix<double, Eigen::ColMajor> QQ = (6*(1-parameter)) * (Q.transpose() * D * Q) + parameter * R;


	//u=2*((QQ+QQ')\diff(diff(yi)./dx))
	vector<double> firstDiff = MathOperationsCPU::diff(mvar);
	for(int i=0; i<(int)firstDiff.size(); i++)
		firstDiff[i] /= dx[i];

	vector<double> secondDiff = MathOperationsCPU::diff(firstDiff);

	Eigen::VectorXd vetorSolve(secondDiff.size());
	for(int i=0; i<(int)secondDiff.size(); i++)
		vetorSolve(i) = secondDiff[i];

	Eigen::SparseMatrix<double, Eigen::ColMajor> matrixSolve = QQ + (Eigen::SparseMatrix<double, Eigen::ColMajor>)QQ.transpose();
	matrixSolve.makeCompressed();

	//Eigen::SparseLU<Eigen::SparseMatrix<double, Eigen::ColMajor>> solver;
	Eigen::ConjugateGradient<Eigen::SparseMatrix<double, Eigen::ColMajor>> solver;
	solver.setMaxIterations(100);

	solver.compute(matrixSolve);

	Eigen::VectorXd u(secondDiff.size());
	u = solver.solve(vetorSolve);

	//ai=yi-6*(1-p)*D*diff([0;diff([0;u;0])./dx;0]);
	Eigen::VectorXd concat(u.size()+2);
	concat(0) = 0;
	for(int i=0; i<u.size(); i++)
		concat(i+1) = 2 * u(i);
	concat(concat.size()-1) = 0;

	Eigen::VectorXd diffConc = MathOperationsCPU::diff(concat);
	for(int i=0; i<diffConc.size(); i++)
		diffConc(i) /= 1;

	Eigen::VectorXd concat2(concat.size()+2);
	concat2(0) = 0;
	for(int i=0; i<u.size(); i++)
		concat2(i+1) = concat(i);
	concat2(concat2.size()-1) = 0;

	Eigen::VectorXd diffConc2 = MathOperationsCPU::diff(concat2);

	Eigen::VectorXd ai(mvar.size());
	for(int i=0; i<(int)mvar.size(); i++)
		ai(i) = mvar[i] - (6*(1-parameter)*diffConc2(i));

	vector<double> result(ai.size()-1);
	for(int i=0; i<ai.size()-1; i++)
		result[i] = ai(i);

	return result;
}

vector<double> SegmentCPU::findEffects()
{
	vector<double> effects(histograms.size(), 1.0);

	//find fade effects
	double maxVariance = *max_element(variances.begin(), variances.end());
	for(int i=0; i<(int)variances.size(); i++)
	{
		double v = variances[i] / maxVariance;
		if(v < 0.1) effects[i] = 0.0;
	}

	//filter dissimilarity vector to find false shot cuts
	vector<double> filter20 = filterDissimilarity(20);
	for(int i=0; i<(int)filter20.size(); i++)
	{
		if(filter20[i] > 0.4) effects[i] = 0.0;
	}

	//find dissolve effects
	vector<vector<int>> dissolveIndexes = dissolveDetectionIndexes();

	if(dissolveIndexes.size() != 0)
	{
		vector<vector<int>> realDissolve = mergeIntervals(dissolveIndexes, 10);
		for(auto i : realDissolve)
		{
			for(int j=i[0]; j<=i[1]; j++)
				effects[j] = 0.0;
		}
	}
	return effects;
}
