<template>
  <div class="certificate-tool">
    <n-tabs v-model:value="activeTab" type="segment">
      <n-tab-pane name="generate" tab="生成">
        <div class="certificate-grid">
          <n-card class="tool-panel" title="证书信息" embedded>
            <n-space vertical :size="16">
              <n-form label-placement="top">
                <div class="certificate-form-grid">
                  <n-form-item label="通用名称 CN">
                    <n-input v-model:value="generateForm.subject.commonName" placeholder="example.com" />
                  </n-form-item>
                  <n-form-item label="组织 O">
                    <n-input v-model:value="generateForm.subject.organization" placeholder="Space Station" />
                  </n-form-item>
                  <n-form-item label="组织单元 OU">
                    <n-input v-model:value="generateForm.subject.organizationalUnit" placeholder="Platform" />
                  </n-form-item>
                  <n-form-item label="国家 C">
                    <n-input v-model:value="generateForm.subject.country" maxlength="2" placeholder="CN" />
                  </n-form-item>
                  <n-form-item label="省/州 ST">
                    <n-input v-model:value="generateForm.subject.state" placeholder="Shanghai" />
                  </n-form-item>
                  <n-form-item label="城市 L">
                    <n-input v-model:value="generateForm.subject.locality" placeholder="Shanghai" />
                  </n-form-item>
                </div>

                <div class="certificate-form-grid compact">
                  <n-form-item label="类型">
                    <n-switch v-model:value="generateForm.isCa">
                      <template #checked>CA</template>
                      <template #unchecked>叶子证书</template>
                    </n-switch>
                  </n-form-item>
                  <n-form-item label="有效天数">
                    <n-input-number v-model:value="generateForm.validDays" :min="1" :max="generateForm.isCa ? 36500 : 825" />
                  </n-form-item>
                  <n-form-item label="密钥算法">
                    <n-select v-model:value="generateForm.keyAlgorithm" :options="keyAlgorithmOptions" />
                  </n-form-item>
                  <n-form-item v-if="generateForm.keyAlgorithm === 'rsa'" label="RSA 位数">
                    <n-input-number v-model:value="generateForm.keyBits" :min="2048" :max="8192" :step="1024" />
                  </n-form-item>
                  <n-form-item v-else label="EC 曲线">
                    <n-select v-model:value="generateForm.ecCurve" :options="curveOptions" />
                  </n-form-item>
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        序列号
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          留空时后端会生成随机正整数；手动填写时请使用十进制整数。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-input v-model:value="generateForm.serialNumber" placeholder="留空自动生成" />
                  </n-form-item>
                </div>

                <div class="san-grid">
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        SAN DNS
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          Subject Alternative Name，浏览器和多数客户端会优先校验这里的域名。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-input v-model:value="generateSan.dns" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个域名&#10;example.com&#10;*.example.com" />
                  </n-form-item>
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        SAN IP
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          需要用 IP 直接访问证书服务时填写，每行一个 IPv4 或 IPv6 地址。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-input v-model:value="generateSan.ips" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个 IP&#10;127.0.0.1&#10;192.168.1.10" />
                  </n-form-item>
                  <n-form-item label="SAN Email">
                    <n-input v-model:value="generateSan.emails" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个邮箱&#10;admin@example.com" />
                  </n-form-item>
                  <n-form-item label="SAN URI">
                    <n-input v-model:value="generateSan.uris" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个 URI&#10;spiffe://example/service" />
                  </n-form-item>
                </div>

                <div class="certificate-form-grid compact">
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        Key Usage
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          限制证书密钥的基础用途。TLS 服务常用 digitalSignature 和 keyEncipherment；CA 常用 keyCertSign 和 cRLSign。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-select v-model:value="generateForm.keyUsage" multiple :options="keyUsageOptions" />
                  </n-form-item>
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        Extended Key Usage
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          进一步声明证书场景。服务端 TLS 选 serverAuth，客户端证书选 clientAuth。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-select v-model:value="generateForm.extendedKeyUsage" multiple :options="extendedKeyUsageOptions" />
                  </n-form-item>
                </div>
              </n-form>

              <div class="action-row">
                <n-button type="primary" :loading="generating" @click="generate">生成一整套</n-button>
                <n-button tertiary @click="loadGenerateExample">填入示例</n-button>
                <n-button quaternary @click="clearGenerateResult">清空结果</n-button>
              </div>
              <n-alert v-if="generateMessage" :type="generateMessage.type" :show-icon="false">
                {{ generateMessage.text }}
              </n-alert>
            </n-space>
          </n-card>

          <n-card class="tool-panel" title="生成结果" embedded>
            <div class="result-actions">
              <n-button type="primary" :disabled="generateOutputItems.length === 0" @click="downloadGeneratedBundleZip">
                下载压缩包
              </n-button>
            </div>
            <pem-result-list :items="generateOutputItems" @copy="copyPem" @download="downloadPem" />
          </n-card>
        </div>
      </n-tab-pane>

      <n-tab-pane name="sign" tab="CA 签名">
        <div class="certificate-grid">
          <n-card class="tool-panel" title="签名输入" embedded>
            <n-space vertical :size="16">
              <n-form label-placement="top">
                <n-form-item>
                  <template #label>
                    <span class="field-label">
                      CA 证书 PEM
                      <n-tooltip trigger="hover">
                        <template #trigger><span class="help-dot">?</span></template>
                        用来作为签发者的 CA 证书，通常是 .crt、.cer 或 .pem 文件。
                      </n-tooltip>
                    </span>
                  </template>
                  <div class="pem-input-stack">
                    <n-input v-model:value="signForm.caCertificatePem" class="pem-input" type="textarea" :autosize="{ minRows: 6 }" placeholder="粘贴 CA 证书 PEM，或点击下方按钮导入文件。" />
                    <n-button tertiary @click="caCertFileInput?.click()">导入 CA 证书</n-button>
                    <input ref="caCertFileInput" class="hidden-file-input" type="file" accept=".pem,.crt,.cer,.txt" @change="importPemFile($event, 'caCertificatePem')" />
                  </div>
                </n-form-item>
                <n-form-item>
                  <template #label>
                    <span class="field-label">
                      CA 私钥 PEM
                      <n-tooltip trigger="hover">
                        <template #trigger><span class="help-dot">?</span></template>
                        与 CA 证书匹配的私钥。它用于签名，不能是待签名证书的私钥。
                      </n-tooltip>
                    </span>
                  </template>
                  <div class="pem-input-stack">
                    <n-input v-model:value="signForm.caPrivateKeyPem" class="pem-input" type="textarea" :autosize="{ minRows: 6 }" placeholder="粘贴 CA 私钥 PEM，或点击下方按钮导入文件。" />
                    <n-button tertiary @click="caKeyFileInput?.click()">导入 CA 私钥</n-button>
                    <input ref="caKeyFileInput" class="hidden-file-input" type="file" accept=".pem,.key,.txt" @change="importPemFile($event, 'caPrivateKeyPem')" />
                  </div>
                </n-form-item>
                <n-form-item>
                  <template #label>
                    <span class="field-label">
                      待签名 CSR PEM
                      <n-tooltip trigger="hover">
                        <template #trigger><span class="help-dot">?</span></template>
                        CSR 内含待签名证书的公钥和主题信息；签发时不需要上传对方私钥。
                      </n-tooltip>
                    </span>
                  </template>
                  <div class="pem-input-stack">
                    <n-input v-model:value="signForm.csrPem" class="pem-input" type="textarea" :autosize="{ minRows: 6 }" placeholder="粘贴 CSR PEM，或点击下方按钮导入 .csr/.pem 文件。" />
                    <n-button tertiary :loading="parsingCsr" :disabled="!signForm.csrPem" @click="parseCurrentCsr(true)">识别 CSR</n-button>
                    <n-button tertiary @click="csrFileInput?.click()">导入 CSR</n-button>
                    <input ref="csrFileInput" class="hidden-file-input" type="file" accept=".csr,.pem,.txt" @change="importPemFile($event, 'csrPem')" />
                  </div>
                </n-form-item>

                <div class="certificate-form-grid compact">
                  <n-form-item label="有效天数">
                    <n-input-number v-model:value="signForm.validDays" :min="1" :max="825" />
                  </n-form-item>
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        序列号
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          留空时后端会生成随机正整数；手动填写时请使用十进制整数。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-input v-model:value="signForm.serialNumber" placeholder="留空自动生成" />
                  </n-form-item>
                </div>

                <div class="san-grid">
                  <n-form-item label="签发 SAN DNS">
                    <n-input v-model:value="signSan.dns" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个域名&#10;service.internal&#10;localhost" />
                  </n-form-item>
                  <n-form-item label="签发 SAN IP">
                    <n-input v-model:value="signSan.ips" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个 IP&#10;127.0.0.1" />
                  </n-form-item>
                  <n-form-item label="签发 SAN Email">
                    <n-input v-model:value="signSan.emails" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个邮箱" />
                  </n-form-item>
                  <n-form-item label="签发 SAN URI">
                    <n-input v-model:value="signSan.uris" type="textarea" :autosize="{ minRows: 3 }" placeholder="每行一个 URI" />
                  </n-form-item>
                </div>

                <div class="certificate-form-grid compact">
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        Key Usage
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          签发叶子证书时通常选择 digitalSignature 和 keyEncipherment。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-select v-model:value="signForm.keyUsage" multiple :options="keyUsageOptions" />
                  </n-form-item>
                  <n-form-item>
                    <template #label>
                      <span class="field-label">
                        Extended Key Usage
                        <n-tooltip trigger="hover">
                          <template #trigger><span class="help-dot">?</span></template>
                          服务端证书选 serverAuth；mTLS 客户端证书选 clientAuth。
                        </n-tooltip>
                      </span>
                    </template>
                    <n-select v-model:value="signForm.extendedKeyUsage" multiple :options="extendedKeyUsageOptions" />
                  </n-form-item>
                </div>
              </n-form>

              <div class="action-row">
                <n-button type="primary" :loading="signing" @click="sign">签发证书</n-button>
                <n-button tertiary :disabled="!generateResult" @click="useGeneratedCsr">使用生成 CSR</n-button>
                <n-button tertiary :disabled="!generateResult" @click="useGeneratedAsCa">使用生成结果作为 CA</n-button>
                <n-button quaternary @click="clearSignResult">清空结果</n-button>
              </div>
              <n-alert v-if="signMessage" :type="signMessage.type" :show-icon="false">
                {{ signMessage.text }}
              </n-alert>
            </n-space>
          </n-card>

          <n-card class="tool-panel" title="签发结果" embedded>
            <pem-result-list :items="signOutputItems" @copy="copyPem" @download="downloadPem" />
          </n-card>
        </div>
      </n-tab-pane>

      <n-tab-pane name="p12" tab="P12">
        <div class="certificate-grid">
          <n-card class="tool-panel" title="P12 输入" embedded>
            <n-space vertical :size="16">
              <n-form label-placement="top">
                <n-form-item label="证书 PEM">
                  <div class="pem-input-stack">
                    <n-input v-model:value="p12Form.certificatePem" class="pem-input" type="textarea" :autosize="{ minRows: 6 }" placeholder="粘贴证书 PEM，或导入 .pem/.crt/.cer 文件。" />
                    <n-button tertiary @click="p12CertFileInput?.click()">导入证书</n-button>
                    <input ref="p12CertFileInput" class="hidden-file-input" type="file" accept=".pem,.crt,.cer,.txt" @change="importP12PemFile($event, 'certificatePem')" />
                  </div>
                </n-form-item>
                <n-form-item label="私钥 PEM">
                  <div class="pem-input-stack">
                    <n-input v-model:value="p12Form.privateKeyPem" class="pem-input" type="textarea" :autosize="{ minRows: 6 }" placeholder="粘贴私钥 PEM，或导入 .pem/.key 文件。" />
                    <n-button tertiary @click="p12KeyFileInput?.click()">导入私钥</n-button>
                    <input ref="p12KeyFileInput" class="hidden-file-input" type="file" accept=".pem,.key,.txt" @change="importP12PemFile($event, 'privateKeyPem')" />
                  </div>
                </n-form-item>
                <n-form-item label="CA 链 PEM">
                  <div class="pem-input-stack">
                    <n-input v-model:value="p12Form.caCertificatePem" class="pem-input" type="textarea" :autosize="{ minRows: 5 }" placeholder="可选。可粘贴一张或多张 CA 证书 PEM。" />
                    <n-button tertiary @click="p12CaFileInput?.click()">导入 CA 链</n-button>
                    <input ref="p12CaFileInput" class="hidden-file-input" type="file" accept=".pem,.crt,.cer,.txt" @change="importP12PemFile($event, 'caCertificatePem')" />
                  </div>
                </n-form-item>

                <div class="certificate-form-grid compact">
                  <n-form-item label="友好名称">
                    <n-input v-model:value="p12Form.friendlyName" placeholder="space-station" />
                  </n-form-item>
                  <n-form-item label="导出密码">
                    <n-input v-model:value="p12Form.password" type="password" show-password-on="click" placeholder="可留空" />
                  </n-form-item>
                </div>
              </n-form>

              <div class="action-row">
                <n-button type="primary" :loading="creatingP12" :disabled="!p12Form.certificatePem || !p12Form.privateKeyPem" @click="createP12Now">合成 P12</n-button>
                <n-button tertiary :disabled="!generateResult" @click="useGeneratedForP12">使用生成结果</n-button>
                <n-button tertiary :disabled="!signResult || !generateResult" @click="useSignedForP12">使用签发结果</n-button>
                <n-button quaternary @click="clearP12">清空</n-button>
              </div>
              <n-alert v-if="p12Message" :type="p12Message.type" :show-icon="false">
                {{ p12Message.text }}
              </n-alert>
            </n-space>
          </n-card>

          <n-card class="tool-panel" title="P12 结果" embedded>
            <pre class="pem-output empty">{{ p12Result?.ok ? `${p12Result.filename} 已生成并下载。` : "P12 结果会下载到本地。" }}</pre>
          </n-card>
        </div>
      </n-tab-pane>

      <n-tab-pane name="parse" tab="解析">
        <div class="certificate-grid">
          <n-card class="tool-panel" title="证书输入" embedded>
            <n-space vertical :size="16">
              <n-form label-placement="top">
                <n-form-item>
                  <template #label>
                    <span class="field-label">
                      证书 PEM
                      <n-tooltip trigger="hover">
                        <template #trigger><span class="help-dot">?</span></template>
                        支持 PEM 格式 X.509 证书，通常以 BEGIN CERTIFICATE 开头。
                      </n-tooltip>
                    </span>
                  </template>
                  <div class="pem-input-stack">
                    <n-input v-model:value="parseCertificatePem" class="pem-input" type="textarea" :autosize="{ minRows: 16 }" placeholder="粘贴证书 PEM，或点击下方按钮导入 .pem/.crt/.cer 文件。" />
                    <n-button tertiary @click="parseCertFileInput?.click()">导入证书</n-button>
                    <input ref="parseCertFileInput" class="hidden-file-input" type="file" accept=".pem,.crt,.cer,.txt" @change="importParseCertificateFile" />
                  </div>
                </n-form-item>
                <n-form-item label="P12 文件">
                  <div class="pem-input-stack">
                    <n-input :value="parseP12Filename || '未选择 P12 文件'" readonly />
                    <n-button tertiary @click="parseP12FileInput?.click()">导入 P12</n-button>
                    <input ref="parseP12FileInput" class="hidden-file-input" type="file" accept=".p12,.pfx,application/x-pkcs12" @change="importParseP12File" />
                  </div>
                </n-form-item>
                <n-form-item label="P12 密码">
                  <n-input v-model:value="parseP12Password" type="password" show-password-on="click" placeholder="无密码可留空" />
                </n-form-item>
              </n-form>

              <div class="action-row">
                <n-button type="primary" :loading="parsing" @click="parseCertificateNow">解析证书</n-button>
                <n-button type="primary" secondary :loading="parsingP12" :disabled="!parseP12Base64" @click="parseP12Now">解析 P12</n-button>
                <n-button tertiary :disabled="!generateResult" @click="useGeneratedCertificateForParse">解析生成的证书</n-button>
                <n-button tertiary :disabled="!signResult" @click="useSignedCertificateForParse">解析签发结果</n-button>
                <n-button quaternary @click="clearParseResult">清空</n-button>
              </div>
              <n-alert v-if="parseMessage" :type="parseMessage.type" :show-icon="false">
                {{ parseMessage.text }}
              </n-alert>
            </n-space>
          </n-card>

          <n-card class="tool-panel" title="解析结果" embedded>
            <div v-if="parseP12Result?.ok" class="certificate-info">
              <section>
                <h3>P12 摘要</h3>
                <div class="info-grid">
                  <span>友好名称</span><code>{{ parseP12Result.friendlyName || "-" }}</code>
                  <span>私钥</span><code>{{ parseP12Result.hasPrivateKey ? `${parseP12Result.privateKeyAlgorithm} / ${parseP12Result.privateKeyBits} bits` : "无" }}</code>
                  <span>证书数量</span><code>{{ parseP12Result.certificateCount }}</code>
                  <span>CA 链数量</span><code>{{ parseP12Result.caCertificateCount }}</code>
                </div>
              </section>

              <section v-for="(certificate, index) in parseP12Result.certificates" :key="`${certificate.role}-${certificate.serialNumberHex}-${index}`">
                <h3>{{ certificate.role === "certificate" ? "主证书" : `CA 证书 ${index}` }}</h3>
                <div class="info-grid">
                  <span>Subject</span><code>{{ certificate.subject.raw }}</code>
                  <span>Issuer</span><code>{{ certificate.issuer.raw }}</code>
                  <span>序列号 HEX</span><code>{{ certificate.serialNumberHex }}</code>
                  <span>有效期</span><code>{{ certificate.validFrom }} / {{ certificate.validTo }}</code>
                  <span>公钥</span><code>{{ certificate.publicKeyAlgorithm }} / {{ certificate.publicKeyBits }} bits</code>
                  <span>CA</span><code>{{ certificate.isCa ? "是" : "否" }}</code>
                  <span>SAN DNS</span><code>{{ joinValues(certificate.san.dns) }}</code>
                  <span>Key Usage</span><code>{{ certificate.keyUsage || "-" }}</code>
                  <span>Extended Key Usage</span><code>{{ certificate.extendedKeyUsage || "-" }}</code>
                </div>
              </section>
            </div>
            <div v-else-if="parseResult?.ok" class="certificate-info">
              <section>
                <h3>摘要</h3>
                <div class="info-grid">
                  <span>版本</span><code>v{{ parseResult.version }}</code>
                  <span>序列号</span><code>{{ parseResult.serialNumber }}</code>
                  <span>序列号 HEX</span><code>{{ parseResult.serialNumberHex }}</code>
                  <span>有效期起</span><code>{{ parseResult.validFrom }}</code>
                  <span>有效期止</span><code>{{ parseResult.validTo }}</code>
                  <span>签名算法</span><code>{{ parseResult.signatureAlgorithm }}</code>
                  <span>公钥</span><code>{{ parseResult.publicKeyAlgorithm }} / {{ parseResult.publicKeyBits }} bits</code>
                  <span>CA</span><code>{{ parseResult.isCa ? "是" : "否" }}</code>
                  <span>自签名</span><code>{{ parseResult.selfSigned ? "是" : "否" }}</code>
                </div>
              </section>

              <section>
                <h3>Subject</h3>
                <div class="info-grid">
                  <span>CN</span><code>{{ parseResult.subject.commonName || "-" }}</code>
                  <span>O</span><code>{{ parseResult.subject.organization || "-" }}</code>
                  <span>OU</span><code>{{ parseResult.subject.organizationalUnit || "-" }}</code>
                  <span>C / ST / L</span><code>{{ subjectLocation(parseResult.subject) }}</code>
                  <span>Raw</span><code>{{ parseResult.subject.raw }}</code>
                </div>
              </section>

              <section>
                <h3>Issuer</h3>
                <div class="info-grid">
                  <span>CN</span><code>{{ parseResult.issuer.commonName || "-" }}</code>
                  <span>O</span><code>{{ parseResult.issuer.organization || "-" }}</code>
                  <span>Raw</span><code>{{ parseResult.issuer.raw }}</code>
                </div>
              </section>

              <section>
                <h3>SAN</h3>
                <div class="info-grid">
                  <span>DNS</span><code>{{ joinValues(parseResult.san.dns) }}</code>
                  <span>IP</span><code>{{ joinValues(parseResult.san.ips) }}</code>
                  <span>Email</span><code>{{ joinValues(parseResult.san.emails) }}</code>
                  <span>URI</span><code>{{ joinValues(parseResult.san.uris) }}</code>
                </div>
              </section>

              <section>
                <h3>扩展</h3>
                <div class="info-grid">
                  <span>Basic Constraints</span><code>{{ parseResult.basicConstraints || "-" }}</code>
                  <span>Key Usage</span><code>{{ parseResult.keyUsage || "-" }}</code>
                  <span>Extended Key Usage</span><code>{{ parseResult.extendedKeyUsage || "-" }}</code>
                </div>
              </section>
            </div>
            <pre v-else class="pem-output empty">证书信息会显示在这里。</pre>
          </n-card>
        </div>
      </n-tab-pane>
    </n-tabs>
  </div>
</template>

<script setup lang="ts">
import { computed, defineComponent, h, reactive, ref, watch } from "vue";
import {
  NAlert,
  NButton,
  NCard,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NSelect,
  NSpace,
  NSwitch,
  NTabPane,
  NTabs,
  NTooltip,
  useMessage,
} from "naive-ui";
import {
  createP12,
  generateCertificateBundle,
  parseP12,
  parseCsr,
  parseCertificate,
  signCertificateRequest,
  type CreateP12Request,
  type CreateP12Response,
  type GenerateCertificateRequest,
  type GenerateCertificateResponse,
  type ParsedCertificateResponse,
  type ParsedP12Response,
  type SignCertificateRequest,
  type SignCertificateResponse,
  type CertificateSubject,
} from "@/api";
import { writeClipboard } from "@/utils/clipboard";
import { createZipBlob } from "@/utils/zip";

type MessageState = { type: "success" | "warning" | "error"; text: string };
type SanText = { dns: string; ips: string; emails: string; uris: string };
type PemItem = { title: string; value: string; filename: string };
type PemInputField = "caCertificatePem" | "caPrivateKeyPem" | "csrPem";
type P12PemInputField = "certificatePem" | "privateKeyPem" | "caCertificatePem";

const PemResultList = defineComponent({
  name: "PemResultList",
  props: {
    items: {
      type: Array<PemItem>,
      required: true,
    },
  },
  emits: ["copy", "download"],
  setup(props, { emit }) {
    return () =>
      h(
        "div",
        { class: "pem-result-list" },
        props.items.length
          ? props.items.map((item) =>
              h("section", { class: "pem-result-block", key: item.title }, [
                h("div", { class: "pem-result-title" }, [
                  h("span", item.title),
                  h(
                    NButton,
                    {
                      size: "small",
                      tertiary: true,
                      onClick: () => emit("copy", item.value),
                    },
                    { default: () => "复制" },
                  ),
                  h(
                    NButton,
                    {
                      size: "small",
                      tertiary: true,
                      onClick: () => emit("download", item),
                    },
                    { default: () => "下载" },
                  ),
                ]),
                h("pre", { class: "pem-output" }, item.value),
              ]),
            )
          : [h("pre", { class: "pem-output empty" }, "结果会显示在这里。")],
      );
  },
});

const activeTab = ref<"generate" | "sign" | "p12" | "parse">("generate");
const generating = ref(false);
const signing = ref(false);
const parsing = ref(false);
const parsingCsr = ref(false);
const creatingP12 = ref(false);
const parsingP12 = ref(false);
const generateResult = ref<GenerateCertificateResponse | null>(null);
const signResult = ref<SignCertificateResponse | null>(null);
const parseResult = ref<ParsedCertificateResponse | null>(null);
const parseP12Result = ref<ParsedP12Response | null>(null);
const p12Result = ref<CreateP12Response | null>(null);
const generateMessage = ref<MessageState | null>(null);
const signMessage = ref<MessageState | null>(null);
const parseMessage = ref<MessageState | null>(null);
const p12Message = ref<MessageState | null>(null);
const caCertFileInput = ref<HTMLInputElement | null>(null);
const caKeyFileInput = ref<HTMLInputElement | null>(null);
const csrFileInput = ref<HTMLInputElement | null>(null);
const parseCertFileInput = ref<HTMLInputElement | null>(null);
const parseP12FileInput = ref<HTMLInputElement | null>(null);
const p12CertFileInput = ref<HTMLInputElement | null>(null);
const p12KeyFileInput = ref<HTMLInputElement | null>(null);
const p12CaFileInput = ref<HTMLInputElement | null>(null);
const parseCertificatePem = ref("");
const parseP12Base64 = ref("");
const parseP12Filename = ref("");
const parseP12Password = ref("");
const toast = useMessage();
let csrParseTimer: ReturnType<typeof setTimeout> | null = null;
let lastParsedCsr = "";

const generateForm = reactive<GenerateCertificateRequest>({
  subject: {
    commonName: "example.com",
    organization: "Space Station",
    organizationalUnit: "Platform",
    country: "CN",
    state: "Shanghai",
    locality: "Shanghai",
  },
  san: {},
  isCa: false,
  validDays: 825,
  keyAlgorithm: "rsa",
  keyBits: 2048,
  ecCurve: "prime256v1",
  keyUsage: ["critical", "digitalSignature", "keyEncipherment"],
  extendedKeyUsage: ["serverAuth", "clientAuth"],
});

const generateSan = reactive<SanText>({
  dns: "example.com\nlocalhost",
  ips: "127.0.0.1",
  emails: "",
  uris: "",
});

const signForm = reactive<SignCertificateRequest>({
  caCertificatePem: "",
  caPrivateKeyPem: "",
  csrPem: "",
  san: {},
  validDays: 825,
  keyUsage: ["critical", "digitalSignature", "keyEncipherment"],
  extendedKeyUsage: ["serverAuth", "clientAuth"],
});

const signSan = reactive<SanText>({
  dns: "",
  ips: "",
  emails: "",
  uris: "",
});

const p12Form = reactive<CreateP12Request>({
  certificatePem: "",
  privateKeyPem: "",
  caCertificatePem: "",
  password: "",
  friendlyName: "space-station",
});

const keyAlgorithmOptions = [
  { label: "RSA", value: "rsa" },
  { label: "ECDSA", value: "ec" },
];

const curveOptions = [
  { label: "prime256v1 / P-256", value: "prime256v1" },
  { label: "secp384r1 / P-384", value: "secp384r1" },
  { label: "secp521r1 / P-521", value: "secp521r1" },
];

const keyUsageOptions = [
  { label: "critical", value: "critical" },
  { label: "digitalSignature", value: "digitalSignature" },
  { label: "keyEncipherment", value: "keyEncipherment" },
  { label: "dataEncipherment", value: "dataEncipherment" },
  { label: "keyAgreement", value: "keyAgreement" },
  { label: "keyCertSign", value: "keyCertSign" },
  { label: "cRLSign", value: "cRLSign" },
];

const extendedKeyUsageOptions = [
  { label: "serverAuth", value: "serverAuth" },
  { label: "clientAuth", value: "clientAuth" },
  { label: "codeSigning", value: "codeSigning" },
  { label: "emailProtection", value: "emailProtection" },
  { label: "timeStamping", value: "timeStamping" },
  { label: "OCSPSigning", value: "OCSPSigning" },
];

watch(
  () => signForm.csrPem,
  (value) => {
    scheduleCsrParse(value);
  },
);

const generateOutputItems = computed<PemItem[]>(() => {
  if (!generateResult.value?.ok) {
    return [];
  }
  return [
    { title: "证书 PEM", value: generateResult.value.certificatePem, filename: "certificate.pem" },
    { title: "私钥 PEM", value: generateResult.value.privateKeyPem, filename: "private-key.pem" },
    { title: "公钥 PEM", value: generateResult.value.publicKeyPem, filename: "public-key.pem" },
    { title: "CSR PEM", value: generateResult.value.csrPem, filename: "request.csr" },
  ];
});

const signOutputItems = computed<PemItem[]>(() => {
  if (!signResult.value?.ok) {
    return [];
  }
  return [{ title: "签发证书 PEM", value: signResult.value.certificatePem, filename: "signed-certificate.pem" }];
});

async function generate() {
  generating.value = true;
  generateMessage.value = null;
  try {
    const payload: GenerateCertificateRequest = {
      ...generateForm,
      subject: { ...generateForm.subject },
      san: buildSan(generateSan),
      keyUsage: generateForm.keyUsage,
      extendedKeyUsage: generateForm.isCa ? [] : generateForm.extendedKeyUsage,
      serialNumber: generateForm.serialNumber || undefined,
    };
    if (payload.isCa) {
      payload.validDays = Math.max(payload.validDays, 3650);
      payload.keyUsage = ["critical", "keyCertSign", "cRLSign"];
    } else {
      payload.validDays = Math.min(payload.validDays, 825);
    }
    generateResult.value = await generateCertificateBundle(payload);
    generateMessage.value = generateResult.value.ok
      ? { type: "success", text: "证书、CSR、公钥和私钥已生成。" }
      : { type: "error", text: generateResult.value.error || "生成失败。" };
  } catch {
    generateMessage.value = { type: "error", text: "生成失败，请确认后端正在运行。" };
  } finally {
    generating.value = false;
  }
}

async function sign() {
  signing.value = true;
  signMessage.value = null;
  try {
    const payload: SignCertificateRequest = {
      ...signForm,
      san: buildSan(signSan),
      keyUsage: signForm.keyUsage,
      extendedKeyUsage: signForm.extendedKeyUsage,
      validDays: Math.min(signForm.validDays, 825),
      serialNumber: signForm.serialNumber || undefined,
    };
    signResult.value = await signCertificateRequest(payload);
    signMessage.value = signResult.value.ok
      ? { type: "success", text: "证书已由 CA 签发。" }
      : { type: "error", text: signResult.value.error || "签发失败。" };
  } catch {
    signMessage.value = { type: "error", text: "签发失败，请确认 CA 证书、私钥和 CSR 都有效。" };
  } finally {
    signing.value = false;
  }
}

async function parseCertificateNow() {
  parsing.value = true;
  parseMessage.value = null;
  try {
    parseResult.value = await parseCertificate({ certificatePem: parseCertificatePem.value });
    parseP12Result.value = null;
    parseMessage.value = parseResult.value.ok
      ? { type: "success", text: "证书已解析。" }
      : { type: "error", text: parseResult.value.error || "解析失败。" };
  } catch {
    parseMessage.value = { type: "error", text: "解析失败，请确认证书 PEM 有效且后端正在运行。" };
  } finally {
    parsing.value = false;
  }
}

async function parseP12Now() {
  if (!parseP12Base64.value) {
    return;
  }

  parsingP12.value = true;
  parseMessage.value = null;
  try {
    parseP12Result.value = await parseP12({
      p12Base64: parseP12Base64.value,
      password: parseP12Password.value || undefined,
    });
    parseResult.value = null;
    parseMessage.value = parseP12Result.value.ok
      ? { type: "success", text: "P12 已解析。" }
      : { type: "error", text: parseP12Result.value.error || "P12 解析失败。" };
  } catch {
    parseMessage.value = { type: "error", text: "P12 解析失败，请确认密码正确且后端正在运行。" };
  } finally {
    parsingP12.value = false;
  }
}

async function createP12Now() {
  creatingP12.value = true;
  p12Message.value = null;
  try {
    p12Result.value = await createP12({
      certificatePem: p12Form.certificatePem,
      privateKeyPem: p12Form.privateKeyPem,
      caCertificatePem: p12Form.caCertificatePem || undefined,
      password: p12Form.password || undefined,
      friendlyName: p12Form.friendlyName || "certificate",
    });
    if (!p12Result.value.ok) {
      p12Message.value = { type: "error", text: p12Result.value.error || "P12 合成失败。" };
      return;
    }

    const filename = p12Result.value.filename || "certificate.p12";
    downloadBase64(p12Result.value.p12Base64, filename, "application/x-pkcs12");
    parseP12Base64.value = p12Result.value.p12Base64;
    parseP12Filename.value = filename;
    parseP12Password.value = p12Form.password || "";
    parseP12Result.value = null;
    p12Message.value = { type: "success", text: `${filename} 已生成。` };
  } catch {
    p12Message.value = { type: "error", text: "P12 合成失败，请确认后端正在运行。" };
  } finally {
    creatingP12.value = false;
  }
}

function buildSan(source: SanText) {
  return {
    dns: parseLines(source.dns),
    ips: parseLines(source.ips),
    emails: parseLines(source.emails),
    uris: parseLines(source.uris),
  };
}

function parseLines(value: string) {
  return value
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
}

async function copyPem(value: string) {
  const copied = await writeClipboard(value);
  const feedback = copied ? "PEM 已复制到剪贴板。" : "当前浏览器不支持剪贴板写入。";
  if (copied) {
    toast.success(feedback);
  } else {
    toast.warning(feedback);
  }
  if (activeTab.value === "generate") {
    generateMessage.value = { type: copied ? "success" : "warning", text: feedback };
  } else {
    signMessage.value = { type: copied ? "success" : "warning", text: feedback };
  }
}

function downloadPem(item: PemItem) {
  const blob = new Blob([item.value], { type: "application/x-pem-file;charset=utf-8" });
  downloadBlob(blob, item.filename);

  const message = `${item.title} 已下载。`;
  if (activeTab.value === "generate") {
    generateMessage.value = { type: "success", text: message };
  } else {
    signMessage.value = { type: "success", text: message };
  }
}

function downloadGeneratedBundleZip() {
  if (!generateOutputItems.value.length) {
    return;
  }

  const blob = createZipBlob(generateOutputItems.value.map((item) => ({ filename: item.filename, content: item.value })));
  downloadBlob(blob, "certificate-bundle.zip");
  const message = "证书压缩包已下载。";
  toast.success(message);
  generateMessage.value = { type: "success", text: message };
}

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

function downloadBase64(base64: string, filename: string, contentType: string) {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; ++i) {
    bytes[i] = binary.charCodeAt(i);
  }
  downloadBlob(new Blob([bytes], { type: contentType }), filename);
}

function arrayBufferToBase64(buffer: ArrayBuffer) {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  const chunkSize = 0x8000;
  for (let i = 0; i < bytes.length; i += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunkSize));
  }
  return btoa(binary);
}

async function importPemFile(event: Event, field: PemInputField) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }

  try {
    signForm[field] = await file.text();
    signMessage.value = { type: "success", text: `${file.name} 已导入。` };
  } catch {
    signMessage.value = { type: "error", text: "文件读取失败，请确认文件内容是文本格式 PEM。" };
  } finally {
    input.value = "";
  }
}

async function importP12PemFile(event: Event, field: P12PemInputField) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }

  try {
    p12Form[field] = await file.text();
    p12Message.value = { type: "success", text: `${file.name} 已导入。` };
  } catch {
    p12Message.value = { type: "error", text: "文件读取失败，请确认文件内容是文本格式 PEM。" };
  } finally {
    input.value = "";
  }
}

function scheduleCsrParse(value: string) {
  if (csrParseTimer) {
    clearTimeout(csrParseTimer);
    csrParseTimer = null;
  }

  if (!isCompleteCsr(value) || value === lastParsedCsr) {
    return;
  }

  csrParseTimer = setTimeout(() => {
    void parseCurrentCsr();
  }, 250);
}

function isCompleteCsr(value: string) {
  return value.includes("-----BEGIN CERTIFICATE REQUEST-----") && value.includes("-----END CERTIFICATE REQUEST-----");
}

async function parseCurrentCsr(force = false) {
  if (!signForm.csrPem) {
    return;
  }
  if (!force && signForm.csrPem === lastParsedCsr) {
    return;
  }

  parsingCsr.value = true;
  try {
    const result = await parseCsr({ csrPem: signForm.csrPem });
    if (!result.ok) {
      signMessage.value = { type: "error", text: result.error || "CSR 识别失败。" };
      return;
    }

    lastParsedCsr = signForm.csrPem;
    signSan.dns = result.san.dns.join("\n");
    signSan.ips = result.san.ips.join("\n");
    signSan.emails = result.san.emails.join("\n");
    signSan.uris = result.san.uris.join("\n");
    if (result.keyUsage.length > 0) {
      signForm.keyUsage = result.keyUsage;
    }
    if (result.extendedKeyUsage.length > 0) {
      signForm.extendedKeyUsage = result.extendedKeyUsage;
    }

    const subject = result.subject.commonName ? `，CN=${result.subject.commonName}` : "";
    signMessage.value = {
      type: result.signatureValid ? "success" : "warning",
      text: `CSR 已识别${subject}，SAN 和用途已填入，可继续修改后签发。`,
    };
  } catch {
    signMessage.value = { type: "error", text: "CSR 识别失败，请确认后端正在运行。" };
  } finally {
    parsingCsr.value = false;
  }
}

async function importParseCertificateFile(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }

  try {
    parseCertificatePem.value = await file.text();
    parseMessage.value = { type: "success", text: `${file.name} 已导入。` };
  } catch {
    parseMessage.value = { type: "error", text: "文件读取失败，请确认文件内容是文本格式证书。" };
  } finally {
    input.value = "";
  }
}

async function importParseP12File(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }

  try {
    parseP12Base64.value = arrayBufferToBase64(await file.arrayBuffer());
    parseP12Filename.value = file.name;
    parseMessage.value = { type: "success", text: `${file.name} 已导入。` };
  } catch {
    parseMessage.value = { type: "error", text: "文件读取失败，请确认文件是 P12/PFX 格式。" };
  } finally {
    input.value = "";
  }
}

function useGeneratedAsCa() {
  if (!generateResult.value?.ok) {
    return;
  }
  signForm.caCertificatePem = generateResult.value.certificatePem;
  signForm.caPrivateKeyPem = generateResult.value.privateKeyPem;
  activeTab.value = "sign";
}

function useGeneratedCsr() {
  if (!generateResult.value?.ok) {
    return;
  }
  signForm.csrPem = generateResult.value.csrPem;
  activeTab.value = "sign";
  void parseCurrentCsr(true);
}

function useGeneratedForP12() {
  if (!generateResult.value?.ok) {
    return;
  }
  p12Form.certificatePem = generateResult.value.certificatePem;
  p12Form.privateKeyPem = generateResult.value.privateKeyPem;
  p12Form.caCertificatePem = "";
  p12Form.friendlyName = generateForm.subject.commonName || "certificate";
  activeTab.value = "p12";
  p12Message.value = { type: "success", text: "已填入生成结果，可设置密码后合成 P12。" };
}

function useSignedForP12() {
  if (!signResult.value?.ok || !generateResult.value?.ok) {
    return;
  }
  p12Form.certificatePem = signResult.value.certificatePem;
  p12Form.privateKeyPem = generateResult.value.privateKeyPem;
  p12Form.caCertificatePem = signForm.caCertificatePem;
  p12Form.friendlyName = generateForm.subject.commonName || "certificate";
  activeTab.value = "p12";
  p12Message.value = { type: "success", text: "已填入签发证书、生成私钥和 CA 链，可合成 P12。" };
}

function useGeneratedCertificateForParse() {
  if (!generateResult.value?.ok) {
    return;
  }
  parseCertificatePem.value = generateResult.value.certificatePem;
  activeTab.value = "parse";
  void parseCertificateNow();
}

function useSignedCertificateForParse() {
  if (!signResult.value?.ok) {
    return;
  }
  parseCertificatePem.value = signResult.value.certificatePem;
  activeTab.value = "parse";
  void parseCertificateNow();
}

function loadGenerateExample() {
  generateForm.subject.commonName = "service.internal";
  generateForm.subject.organization = "Space Station";
  generateForm.subject.organizationalUnit = "Platform";
  generateForm.subject.country = "CN";
  generateForm.subject.state = "Shanghai";
  generateForm.subject.locality = "Shanghai";
  generateForm.isCa = false;
  generateForm.validDays = 825;
  generateForm.keyAlgorithm = "rsa";
  generateForm.keyBits = 2048;
  generateSan.dns = "service.internal\nlocalhost";
  generateSan.ips = "127.0.0.1\n10.0.0.10";
  generateSan.emails = "";
  generateSan.uris = "";
}

function clearGenerateResult() {
  generateResult.value = null;
  generateMessage.value = null;
}

function clearSignResult() {
  signResult.value = null;
  signMessage.value = null;
}

function clearParseResult() {
  parseCertificatePem.value = "";
  parseP12Base64.value = "";
  parseP12Filename.value = "";
  parseP12Password.value = "";
  parseResult.value = null;
  parseP12Result.value = null;
  parseMessage.value = null;
}

function clearP12() {
  p12Form.certificatePem = "";
  p12Form.privateKeyPem = "";
  p12Form.caCertificatePem = "";
  p12Form.password = "";
  p12Result.value = null;
  p12Message.value = null;
}

function joinValues(values?: string[]) {
  return values?.length ? values.join(", ") : "-";
}

function subjectLocation(subject: CertificateSubject) {
  return [subject.country, subject.state, subject.locality].filter(Boolean).join(" / ") || "-";
}
</script>
