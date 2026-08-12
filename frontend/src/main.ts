import { createApp } from "vue";
import App from "./App.vue";
import router from "./router";
import "./styles.css";
import "./registerServiceWorker";
import { installI18n } from "./i18n";

createApp(App).use(router).mount("#app");
installI18n();
