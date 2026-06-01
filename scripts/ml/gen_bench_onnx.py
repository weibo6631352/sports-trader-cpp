import numpy as np, lightgbm as lgb
from onnxmltools import convert_lightgbm
from onnxmltools.convert.common.data_types import FloatTensorType
rng = np.random.default_rng(7)
X = rng.standard_normal((3000, 110)).astype("float32")
y = (1.0/(1.0+np.exp(-(X[:,30]*0.4 + X[:,17]*0.3)))).astype("float32")  # 合成 fair∈(0,1)
m = lgb.LGBMRegressor(n_estimators=100, num_leaves=31, min_child_samples=20, verbose=-1)
m.fit(X, y)
onx = convert_lightgbm(m, initial_types=[("input", FloatTensorType([None,110]))], target_opset=12)
open("models/bench_fair.onnx","wb").write(onx.SerializeToString())
print("生成 models/bench_fair.onnx: 100 trees × 110 feat → 1 output")
